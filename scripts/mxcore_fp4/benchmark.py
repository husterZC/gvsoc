#!/usr/bin/env python3
"""Benchmark three GEMMs with one/four owners in a production Arche3D cluster."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
from reference import FORMATS, BITS, bases, sizes

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
WORK = ROOT / 'build/mxcore_outputs'
WORKLOADS = ((32, 32, 576), (32, 64, 192), (32, 192, 64))
ENV = dict(os.environ)
ENV['PATH'] = str(ROOT / 'install/bin') + os.pathsep + ENV.get('PATH', '')
ENV['PYTHONPATH'] = os.pathsep.join([str(ROOT / 'pulp'), str(ROOT / 'build/arche3d_work/python'),
                                    str(ROOT / 'install/python'), ENV.get('PYTHONPATH', '')])
ENV['USE_GVRUN'] = '1'


def run(command, name, timeout=180):
    result = subprocess.run(list(map(str, command)), cwd=ROOT, env=ENV, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=timeout)
    (WORK / (name + '.log')).write_text(result.stdout)
    if result.returncode:
        raise RuntimeError(f'{name}: {result.stdout[-3000:]}')
    return result.stdout


def shape_name(shape):
    m, n, k = shape
    return f'm{m}_n{n}_k{k}'


def encode_rle(data):
    """Literal tokens 0..127 hold 1..128 bytes; repeat tokens hold 3..130."""
    def repeated(index):
        count = 1
        while count < 130 and index + count < len(data) and data[index + count] == data[index]:
            count += 1
        return count

    encoded = bytearray()
    index = 0
    while index < len(data):
        count = repeated(index)
        if count >= 3:
            encoded.extend((128 + count - 3, data[index]))
            index += count
        else:
            first = index
            index += count
            while index < len(data) and index - first < 128:
                count = repeated(index)
                if count >= 3:
                    break
                index += min(count, 128 - (index - first))
            encoded.append(index - first - 1)
            encoded.extend(data[first:index])
    return encoded


def fixtures(shape, format_id):
    m, n, k = shape
    name = shape_name(shape)
    memory = (WORK / f'mxfp4_{name}.bin').read_bytes()
    ptrs, lengths = bases(m, n, k), sizes(m, n, k)
    fmt = FORMATS[format_id]
    golden = (WORK / f'{fmt}_{name}.expected').read_bytes()
    calibration = json.loads((HERE / 'calibration.json').read_text())
    verified = next(r for r in calibration['records'] if
                    (r['m'], r['n'], r['k'], r['format'], r['seed']) == (m, n, k, fmt, 7))
    if hashlib.sha256(golden).hexdigest() != verified['output_sha256']:
        raise RuntimeError(f'Unverified golden fixture: {fmt}_{name}')
    verified_input = next(r for r in calibration['records'] if
                          (r['m'], r['n'], r['k'], r['format'], r['seed']) == (m, n, k, 'mxfp4', 7))
    if hashlib.sha256(memory).hexdigest() != verified_input['input_sha256']:
        raise RuntimeError(f'Unverified input fixture: {name}')
    planes = max(1, BITS[format_id] // 8)
    planar = b''.join(golden[b::planes] for b in range(planes))
    text = '// Generated from verified RTL fixtures.\n#include <stdint.h>\n'
    text += f'#define BENCH_M {m}\n#define BENCH_N {n}\n#define BENCH_K {k}\n'
    text += f'#define OUTPUT_BYTES {m*n*BITS[format_id]//8}\n'
    text += f'#define EXPECTED_BYTES {len(golden)}\n#define EXPECTED_PLANES {planes}\n'

    def array(label, data):
        return 'static const uint8_t ' + label + '[] = {\n' + ''.join(
            '    ' + ','.join(f'0x{x:02x}' for x in data[i:i+32]) + ',\n'
            for i in range(0, len(data), 32)) + '};\n'

    for i, label in enumerate(('input_a', 'input_b', 'input_sa', 'input_sb')):
        text += array(label, memory[ptrs[i]:ptrs[i]+lengths[i]])
    text += array('expected_rle', encode_rle(planar))
    (WORK / 'fixtures.h').write_text(text)


def main():
    outputs = []
    for shape in WORKLOADS:
        for fmt in range(6):
            label = shape_name(shape) + '_' + FORMATS[fmt]
            fixtures(shape, fmt)
            directory = WORK / ('sdk_benchmark_' + label)
            run(['make', '-C', ROOT / 'arche3d_sdk', f'PYTHON={sys.executable}', 'cfg=default',
                 f'app={HERE / "benchmark"}', f'BUILD={directory}',
                 f'EXTRA_CFLAGS=-I{WORK} -DBENCH_FORMAT={fmt}'], 'benchmark_build_' + label)
            result = run(['gvrun', '--target=arche3d_dma_test', '--target-dir=pulp/tests/arche3d',
                          f'--parameter=config={ROOT / "arche3d_sdk/apps/collective_row_sweep/ram_config.py"}',
                          '--parameter=clusters=1', '--parameter=progress_cycles=0',
                          f'--binary={directory / "benchmark.elf"}',
                          f'--work-dir={WORK / ("cluster_run_" + label)}', 'run'], 'benchmark_' + label)
            status = [json.loads(line.split(' ', 1)[1]) for line in result.splitlines()
                      if line.startswith('ARCHE3D_RESULT ')]
            if 'MX_BENCH_PASS' not in result or len(status) != 1 or status[0]['status'] != 'PASS':
                raise RuntimeError('Missing benchmark PASS: ' + label)
            outputs.append(result)
            print('PASS one/four engines: ' + label, flush=True)
    summarize(''.join(outputs))


def summarize(output):
    names = ('m', 'n', 'k', 'active', 'format_id', 'repeat', 'engine', 'start', 'cycles',
             'nominal_cycles', 'stall_cycles', 'read_bytes', 'write_bytes', 'a_base', 'c_base',
             'software_window', 'software_start', 'software_end')
    raw = []
    for line in output.splitlines():
        if line.startswith('MX_BENCH,'):
            values = list(map(int, line.split(',')[1:]))
            if len(values) != len(names):
                raise RuntimeError('Malformed benchmark measurement')
            raw.append(dict(zip(names, values)))
    if len(raw) != 90 * len(WORKLOADS):
        raise RuntimeError(f'Expected {90 * len(WORKLOADS)} engine measurements, got {len(raw)}')
    records, summary = [], []
    for m, n, k in WORKLOADS:
        macs = m * n * k
        ideal_cycles = macs / 1024
        for active in (1, 4):
            for fmt in range(6):
                group = []
                for repeat in range(3):
                    engines = sorted((r for r in raw if
                        (r['m'], r['n'], r['k'], r['active'], r['format_id'], r['repeat']) ==
                        (m, n, k, active, fmt, repeat)), key=lambda r: r['engine'])
                    if [e['engine'] for e in engines] != list(range(active)):
                        raise RuntimeError('Missing or duplicate engine')
                    start = min(r['start'] for r in engines)
                    end = max(r['start'] + r['cycles'] for r in engines)
                    span = end - start
                    overlap = min(r['start'] + r['cycles'] for r in engines) - max(r['start'] for r in engines)
                    if overlap <= 0:
                        raise RuntimeError('Jobs did not overlap')
                    for r in engines:
                        if r['cycles'] != r['nominal_cycles'] + r['stall_cycles']:
                            raise RuntimeError('Cycle accounting mismatch')
                        expected_read = (m*k//2 + m*k//32)*(n//32) + (n*k//2 + n*k//32)*(m//32)
                        expected_write = m*n*BITS[fmt]//8 + (m*n//32 if fmt == 0 else 0)
                        if (r['read_bytes'], r['write_bytes']) != (expected_read, expected_write):
                            raise RuntimeError('Unexpected GEMM traffic')
                        r['mac_utilization_percent'] = 100 * ideal_cycles / r['cycles']
                        r['window_mac_utilization_percent'] = 100 * ideal_cycles / span
                        r['busy_coverage_percent'] = 100 * r['cycles'] / span
                    record = dict(m=m, n=n, k=k, useful_macs_per_engine=macs, active=active,
                        format=FORMATS[fmt], repeat=repeat, cycles=span, runtime_ns=span,
                        launch_skew_cycles=max(r['start'] for r in engines)-start,
                        all_active_engines_overlap_cycles=overlap,
                        cluster_mac_utilization_percent=100*active*ideal_cycles/(4*span),
                        active_mac_utilization_percent=100*ideal_cycles/span,
                        gmac_per_second=active*macs/span,
                        hwpe_bytes_per_cycle=sum(r['read_bytes']+r['write_bytes'] for r in engines)/span,
                        software_window_cycles=engines[0]['software_window'], engines=engines)
                    records.append(record)
                    group.append(record)
                group.sort(key=lambda r: r['cycles'])
                middle = dict(group[1])
                middle['runtime_cycles_range'] = [group[0]['cycles'], group[-1]['cycles']]
                summary.append(middle)
    report = dict(schema_version=2, status='PASS', clock_hz=1000000000,
        workloads=[dict(m=m,n=n,k=k,useful_macs_per_engine=m*n*k) for m,n,k in WORKLOADS],
        physical_engines=4, peak_macs_per_engine_cycle=1024, hwpe_bytes_per_cycle=512, engine_port_bytes=32,
        memory_backend='memory', full_chip=False, all_output_bytes_verified=True, fixture_seed=7,
        programs='One shape/output pair per ELF; each runs both scenarios three times.',
        calibration_sha256=hashlib.sha256((HERE/'calibration.json').read_bytes()).hexdigest(),
        source_sha256={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest()
                       for p in [ROOT/'pulp/pulp/mxcore_fp4/mxcore_fp4.cpp',
                                 ROOT/'pulp/pulp/mxcore_fp4/compute.hpp',
                                 ROOT/'pulp/pulp/mxcore_fp4/format.hpp',
                                 ROOT/'pulp/pulp/mxcore_fp4/output_profiles.inc',
                                 ROOT/'arche3d_sdk/runtime/mxcore_fp4.c',HERE/'benchmark/main.c']},
        caveat='RTL calibrates unstalled single-engine traces; concurrent stalls use the GVSoC additive '
               'profile-stretching model, not a multi-engine RTL timing validation.',
        summary=summary, runs=records)
    for destination in (WORK/'benchmark.json', HERE/'benchmark.json'):
        destination.write_text(json.dumps(report, indent=2)+'\n')
    write_markdown(summary)
    for r in summary:
        utils = '/'.join(f'{e["mac_utilization_percent"]:.1f}' for e in r['engines'])
        print(f'{r["m"]}x{r["n"]}x{r["k"]} {r["active"]} {r["format"]}: {r["cycles"]} ns, '
              f'engine MAC utilization {utils}%, cluster {r["cluster_mac_utilization_percent"]:.1f}%')


def write_markdown(summary):
    text = '# Arche3D MXCoreFP4 output benchmark\n\n'
    text += ('One production cluster at **1 GHz**, four installed engines, MXFP4 inputs. '
             'Every output byte and output canary passed. Each active engine executes one GEMM. '
             'Rows use the median-runtime repetition out of three. Runtime is earliest '
             'accelerator trigger through latest completion; setup is excluded.\n\n')
    for m,n,k in WORKLOADS:
        rows = [r for r in summary if (r['m'],r['n'],r['k'])==(m,n,k)]
        text += f'## {m}×{n}×{k} (M×N×K; {m*n*k:,} useful MACs per engine)\n\n'
        text += '| Active | Output | Runtime (ns) | Engine 0 / 1 / 2 / 3 MAC utilization (%) | Overall cluster (%) |\n'
        text += '| --- | --- | ---: | --- | ---: |\n'
        for r in rows:
            utils = [f'{e["mac_utilization_percent"]:.2f}' for e in r['engines']] + ['0.00']*(4-r['active'])
            text += f'| {r["active"]} | {r["format"].upper()} | {r["cycles"]} | {" / ".join(utils)} | {r["cluster_mac_utilization_percent"]:.2f} |\n'
        text += '\n| Active | Output | Job cycles, engines 0–3 | Extra memory cycles, engines 0–3 | Launch skew | Runtime range | SDK window cycles | Read+write BW (B/cycle) |\n'
        text += '| --- | --- | --- | --- | ---: | --- | ---: | ---: |\n'
        for r in rows:
            jobs = '/'.join(str(e['cycles']) for e in r['engines'])
            stalls = '/'.join(str(e['stall_cycles']) for e in r['engines'])
            lo,hi = r['runtime_cycles_range']
            text += (f'| {r["active"]} | {r["format"].upper()} | {jobs} | {stalls} | '
                     f'{r["launch_skew_cycles"]} | {lo}–{hi} | {r["software_window_cycles"]} | '
                     f'{r["hwpe_bytes_per_cycle"]:.2f} |\n')
        text += '\n| Output | Unstalled RTL cycles | Input read bytes/job | Output write bytes/job | Four-job throughput speedup |\n'
        text += '| --- | ---: | ---: | ---: | ---: |\n'
        for fmt in FORMATS:
            one = next(r for r in rows if r['active']==1 and r['format']==fmt)
            four = next(r for r in rows if r['active']==4 and r['format']==fmt)
            e = one['engines'][0]
            text += f'| {fmt.upper()} | {e["nominal_cycles"]} | {e["read_bytes"]} | {e["write_bytes"]} | {4*one["cycles"]/four["cycles"]:.3f}× |\n'
        text += '\n'
    text += ('## Interpretation\n\n'
             'Per-engine utilization = `M*N*K / (1024 * engine_cycles)`. Overall cluster '
             'utilization = `active_count * M*N*K / (4 * 1024 * runtime_cycles)`. '
             'Idle engines count as zero. These measure useful MACs relative to peak, '
             'not just the fraction of time the engine is busy.\n\n'
             'Both added shapes perform 393,216 MACs. The 32×192×64 case produces three '
             'times as many output elements as 32×64×192. Their input traffic includes '
             'the RTL streamer’s rereads of A for each N tile, so total matrix read traffic '
             'is 13,056 bytes per job for both shapes. Equal-width format pairs have '
             'identical unstalled RTL schedules; differences between their concurrent '
             'measurements reflect launch timing and arbitration state. Per-run ranges '
             'and per-engine memory-stall counts are included above.\n\n'
             'The SDK window includes launch setup, polling and both barriers. Input '
             'initialization, output checks and printing occur outside it. Idle cores '
             'wait at a hardware barrier; DMA, layout and Spatz are idle. All jobs own '
             'private L1 buffers. Buffer sizes follow each shape, reserving FP32 output '
             'capacity for every output format of that shape. Exact addresses, source '
             'hashes and timestamps are recorded in [benchmark.json](benchmark.json). '
             'Each shape/output pair uses its own ELF within the existing 32 KiB program '
             'stripe. Golden results use lossless byte-plane RLE, decoded into L1 before '
             'timing. Inputs are identical across output formats for a given shape.\n\n'
             'The HWPE fabric ceiling is 512 **bytes**/cycle shared by all engines; each '
             'MXCore has a 32-byte port with one outstanding request. The full JSON also '
             'includes throughput, active-engine normalized utilization and busy coverage.\n\n'
             '**Accuracy scope:** unstalled single-engine schedules are RTL-calibrated. '
             'Concurrent L1 backpressure uses additive schedule stretching in GVSoC; '
             'these results have not been calibrated against a four-engine RTL cluster. '
             'The RAM backend replaces DRAMSys, and measured GEMMs access local L1 only. '
             'See [reproduction and metric definitions](README.md).\n')
    (HERE/'results.md').write_text(text)


if __name__ == '__main__':
    main()
