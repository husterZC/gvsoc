#!/usr/bin/env python3
"""Build the pinned RTL output extension and export only verified transfer profiles."""
import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import subprocess

from reference import SHAPES, FORMATS, make_case
from rtl.prepare import REVISION, prepare

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
LEGACY = ROOT / 'pulp/tests/mxcore_fp4'
MODEL = ROOT / 'pulp/pulp/mxcore_fp4'
EXTENDED_SHAPES = SHAPES + [(32,32,576), (32,192,64)]


def sha(data):
    return hashlib.sha256(data).hexdigest()


def build(args):
    prepare(args.rtl)
    sources = subprocess.check_output(shlex.split(args.bender) +
              ['script', 'verilator', '-t', 'rtl', '-t', 'mxcore_hwpe'], cwd=args.rtl, text=True)
    for key, value in [('VECTOR_SIZE',16), ('NPE',32), ('REUSE',32), ('NUM_PIPE_REGS',4), ('TCDM_BW',256)]:
        sources = re.sub(rf'\+define\+{key}=\d+', f'+define+{key}={value}', sources)
    (args.work/'sources.f').write_text(sources)
    command = shlex.split(args.verilator) + [
        '--cc', '--exe', '--build', '-j', str(args.jobs), '--Mdir', str(args.work/'obj'),
        '--top-module', 'mxcore_hwpe_wrap', '-Wno-fatal', '-Wno-BLKANDNBLK', '--no-timing',
        '-f', str(args.work/'sources.f'), str(LEGACY/'rtl/mxcore_fp4_quantizer.sv'),
        str(HERE/'rtl/mxcore_output_cast.sv'), str(HERE/'rtl/driver.cpp')]
    env = dict(os.environ, CCACHE_DIR=str(args.work/'ccache'), CCACHE_DISABLE='1')
    with (args.work/'build.log').open('w') as log:
        subprocess.run(command, cwd=args.rtl, stdout=log, stderr=subprocess.STDOUT, env=env, check=True)


def run_case(args, m, n, k, fmt, seed, pattern='random'):
    name = f'{FORMATS[fmt]}_m{m}_n{n}_k{k}'
    memory, golden = make_case(m, n, k, seed, pattern, fmt)
    (args.work/'memory.bin').write_bytes(memory)
    result = subprocess.run([str(args.work/'obj/Vmxcore_hwpe_wrap'), str(m), str(n), str(k),
                            str(fmt), str(args.work/'memory.bin'), str(args.work/'output.bin'),
                            str(args.work/'trace.csv')], check=True, capture_output=True,
                            text=True, timeout=120)
    (args.work/f'{name}_{pattern}_{seed}.log').write_text(result.stdout+result.stderr)
    match = re.search(r'MXCORE_RTL,\d+,\d+,\d+,(\d+)', result.stdout)
    if not match:
        raise RuntimeError(f'{name}: missing RTL completion')
    actual = (args.work/'output.bin').read_bytes()
    if actual != golden:
        bad = [i for i,(x,y) in enumerate(zip(actual,golden)) if x!=y]
        raise RuntimeError(f'{name} {pattern} seed {seed}: {len(bad)} mismatches, first {bad[:12]}, '
                           f'lengths {len(actual)}/{len(golden)}')
    subprocess.run([str(args.work/'compute'),str(m),str(n),str(k),str(fmt),
                    str(args.work/'memory.bin'),str(args.work/'compute.bin')],check=True)
    if (args.work/'compute.bin').read_bytes() != golden:
        raise RuntimeError(f'{name} {pattern}: C++ result mismatch')
    trace = (args.work/'trace.csv').read_text()
    record = dict(format=FORMATS[fmt], m=m, n=n, k=k, seed=seed, pattern=pattern,
                  rtl_cycles=int(match[1]), input_sha256=sha(memory), output_sha256=sha(actual),
                  trace_sha256=sha(trace.encode()), mismatched_bytes=0)
    (args.work/f'{name}.bin').write_bytes(memory)
    (args.work/f'{name}.expected').write_bytes(actual)
    (args.work/f'{name}.csv').write_text(trace)
    return record, trace


def sweep(args):
    records, words, profiles, directed = [], [], [], []
    subprocess.run(['c++','-std=c++17','-O2',str(HERE/'compute_cli.cpp'),'-o',str(args.work/'compute')],check=True)
    shapes = [(32,32,576), (32,192,64)] if args.quick else EXTENDED_SHAPES
    cached = None
    if args.reuse_verified:
        cached = json.loads((args.work/'calibration.json').read_text())
        if cached['upstream_revision'] != REVISION:
            raise RuntimeError('Cannot reuse calibration from another RTL revision')
        for path, digest in cached['sources'].items():
            if sha((ROOT/path).read_bytes()) != digest:
                raise RuntimeError(f'Calibration source changed: {path}; run without --reuse-verified')
        if sha((args.rtl/'Bender.lock').read_bytes()) != cached['dependency_lock_sha256']:
            raise RuntimeError('Calibration dependency lock changed')
        patch = subprocess.check_output(['git','diff','--','mxcore-rtl'],cwd=args.rtl)
        fpnew = next(args.rtl.glob('.bender/git/checkouts/fpnew-*/src/mxdotp/fpnew_mxdotp_multi_modules.sv')).parents[2]
        dependency_patch = subprocess.check_output(['git','diff'],cwd=fpnew)
        if sha(patch) != cached['upstream_patch_sha256'] or sha(dependency_patch) != cached['fpnew_patch_sha256']:
            raise RuntimeError('Calibration RTL patches changed')
    reused = 0
    legacy = json.loads((LEGACY/'calibration.json').read_text())
    old = {(r['m'],r['n'],r['k']): r for r in legacy['records'] if r['seed']==1}
    streams = {}
    for fmt in range(6):
        for m,n,k in shapes:
            previous = None
            saved = [] if not cached else [r for r in cached['records']
                if (r['format'],r['m'],r['n'],r['k']) == (FORMATS[fmt],m,n,k)]
            name = f'{FORMATS[fmt]}_m{m}_n{n}_k{k}'
            if saved:
                if sorted(r['seed'] for r in saved) != [1,7]:
                    raise RuntimeError(f'Incomplete cached seeds: {name}')
                trace = (args.work/f'{name}.csv').read_text()
                fixtures = saved + [r for r in cached['directed']
                    if (r['format'],r['m'],r['n'],r['k']) == (FORMATS[fmt],m,n,k)]
                if not any(sha((args.work/f'{name}.bin').read_bytes()) == r['input_sha256'] and
                           sha((args.work/f'{name}.expected').read_bytes()) == r['output_sha256']
                           for r in fixtures):
                    raise RuntimeError(f'Cached fixtures changed: {name}')
                if any(sha(trace.encode()) != r['trace_sha256'] for r in saved):
                    raise RuntimeError(f'Cached trace changed: {name}')
            for seed in (1,7):
                if saved:
                    record = next(r for r in saved if r['seed'] == seed)
                    reused += 1
                else:
                    record, trace = run_case(args,m,n,k,fmt,seed)
                signature = (record['rtl_cycles'],trace)
                if previous is not None and signature != previous:
                    raise RuntimeError('Data-dependent RTL timing')
                previous = signature
                records.append(record)
                if fmt == 0 and seed == 1 and (m,n,k) in old:
                    for field in ('rtl_cycles','trace_sha256','output_sha256'):
                        if record[field] != old[m,n,k][field]:
                            raise RuntimeError(f'Legacy MXFP4 regression: {field}')
            first, last = len(words), 0
            transactions = [tuple(map(int,line.split(','))) for line in trace.splitlines()]
            if trace in streams:
                first = streams[trace]
            else:
                streams[trace] = first
                for cycle,region,offset in transactions:
                    if not (0<cycle-last<65536 and 0<=region<6 and offset%32==0 and offset//32<8192):
                        raise RuntimeError(f'Invalid transfer {cycle,region,offset}')
                    words.append(((cycle-last)<<16)|(region<<13)|(offset//32))
                    last = cycle
            profiles.append((fmt,m,n,k,record['rtl_cycles'],first,len(transactions)))
            print(f'{FORMATS[fmt]} {m}x{n}x{k}: {record["rtl_cycles"]} cycles; '
                  f'two seeds {"reused (hash-verified)" if saved else "pass"}', flush=True)
        for pattern in ('zero','ones','ties','wide','tiny','scale_nan'):
            record = next((r for r in cached['directed'] if r['format']==FORMATS[fmt] and
                           r['pattern']==pattern and (r['m'],r['n'],r['k'],r['seed'])==(32,32,192,42)),None) if cached else None
            if record is None:
                record, _ = run_case(args,32,32,192,fmt,42,pattern)
            else:
                reused += 1
            directed.append(record)
            print(f'{FORMATS[fmt]} directed {pattern}: pass', flush=True)
    # Preserve existing MXFP4 profiles and their consumers; this table adds
    # typed profiles (including the K=576 extension) with its own transfer array.
    body = '// Generated by scripts/mxcore_fp4/calibrate.py after RTL/oracle agreement.\n'
    body += 'inline constexpr uint32_t output_transfers[] = {\n'
    body += ''.join('    '+','.join(f'0x{v:08x}' for v in words[i:i+8])+',\n' for i in range(0,len(words),8))
    body += '};\ninline constexpr OutputProfile output_profiles[] = {\n'
    body += ''.join('    {'+str(p[0])+', {'+','.join(map(str,p[1:]))+'}},\n' for p in profiles)
    body += '};\n'
    patch = subprocess.check_output(['git','diff','--','mxcore-rtl'],cwd=args.rtl)
    fpnew = next(args.rtl.glob('.bender/git/checkouts/fpnew-*/src/mxdotp/fpnew_mxdotp_multi_modules.sv')).parents[2]
    fpnew_patch = subprocess.check_output(['git','diff'],cwd=fpnew)
    report = dict(upstream_revision=REVISION,
                  generated_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
                  reused_verified_jobs=reused,
                  scope='Trigger acceptance to completion, 256-bit port, one-cycle memory, no stalls.',
                  hardware=dict(vector_size=16,npe=32,reuse=32,num_pipe_regs=4,tcdm_bits=256,
                                fp4_values_per_dot=32,output_fifo_depth=2,cast_pipeline_cycles=0),
                  profiles_sha256=sha(body.encode()), dependency_lock_sha256=sha((args.rtl/'Bender.lock').read_bytes()),
                  upstream_patch_sha256=sha(patch), fpnew_patch_sha256=sha(fpnew_patch),
                  sources={str(p.relative_to(ROOT)):sha(p.read_bytes()) for p in
                           [HERE/'rtl/driver.cpp', HERE/'rtl/mxcore_output_cast.sv', HERE/'rtl/prepare.py',
                            HERE/'reference.py', LEGACY/'rtl/mxcore_fp4_quantizer.sv']},
                  verilator=subprocess.check_output(shlex.split(args.verilator)+['--version'],text=True).strip(),
                  records=records,directed=directed)
    (args.work/'output_profiles.inc').write_text(body)
    (args.work/'calibration.json').write_text(json.dumps(report,indent=2)+'\n')
    (args.work/'mxcore.patch').write_bytes(patch)
    (args.work/'fpnew.patch').write_bytes(fpnew_patch)
    if args.export:
        if args.quick:
            raise RuntimeError('Refusing to export an incomplete sweep')
        (MODEL/'output_profiles.inc').write_text(body)
        (HERE/'calibration.json').write_text(json.dumps(report,indent=2)+'\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--rtl',type=Path,default=ROOT/'third_party/MXCore')
    parser.add_argument('--work',type=Path,default=ROOT/'build/mxcore_outputs')
    parser.add_argument('--bender',default='bender-0.28.1')
    parser.add_argument('--verilator',default='verilator-5.020 verilator')
    parser.add_argument('--jobs',type=int,default=8)
    parser.add_argument('--skip-build',action='store_true')
    parser.add_argument('--quick',action='store_true')
    parser.add_argument('--reuse-verified',action='store_true',
                        help='Reuse unchanged RTL records after checking source, dependency and fixture hashes')
    parser.add_argument('--export',action='store_true')
    args = parser.parse_args()
    args.rtl=args.rtl.resolve(); args.work=args.work.resolve(); args.work.mkdir(parents=True,exist_ok=True)
    if not args.skip_build:
        build(args)
    sweep(args)
