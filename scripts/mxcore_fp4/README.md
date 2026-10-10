# MXCoreFP4 output calibration and Arche3D benchmark

This harness extends the pinned MXCore RTL reference to MXFP4, FP32, BF16,
FP16, FP8 E4M3 and FP8 E5M2 outputs. Inputs remain MXFP4 with one E8M0 scale
per 32 elements. FP32 accumulation and the upstream native FP32 output path
are retained. Narrow IEEE conversions use RNE, subnormals and overflow to
infinity; E4M3 is the SDK's IEEE-style format, **not E4M3FN**. Native outputs
have no shared scale.

`rtl/prepare.py` applies the existing MXFP4 fixes, then adds an output selector
in CTRL_ENGINE[25:23] and two narrow output paths (16/8 bits). The converters
are combinational, feeding two-entry result FIFOs and the existing 256-bit
streamer. The MXFP4 path and its original measured traces remain unchanged.
These are calibration extensions to the pinned reference, not upstream RTL
features or a claim about achievable synthesis frequency.

All fetched sources and generated build files stay inside this repository.
Only hardware model code/documentation goes in `pulp`; the SDK changes are
the runtime API and programming documentation. The calibration tools and
benchmark application live here. Results are in `build/mxcore_outputs/`.

## Reproduce

From the root of the enclosing GVSoC repository, with Python 3.10+, C++17,
Bender 0.28.1, Verilator 5.020, and the configured RISC-V compiler in PATH:

```sh
git clone https://github.com/pulp-platform/MXCore.git third_party/MXCore
git -C third_party/MXCore checkout --detach edf45a65a9c19dcb901eed2148ab9621573a6621
(cd third_party/MXCore && bender-0.28.1 checkout)
python scripts/mxcore_fp4/calibrate.py --export
CCACHE_DISABLE=1 python scripts/mxcore_fp4/check_cast.py
```

Use `--verilator verilator` if the compiler is directly on PATH. The default
invocation uses this workspace's `verilator-5.020 verilator` launcher.
`--skip-build` reuses the RTL executable; `--quick` checks the additional
32×32×576 and 32×192×64 shapes plus directed tests and cannot export incomplete
model profiles. `--reuse-verified` reuses existing calibration records only
after checking source, dependency, RTL patch, input/output and trace hashes;
missing shapes are simulated and checked before the combined table is exported.

The complete calibration covers 56 shapes × 6 formats × 2 random seeds against
an independent Python arithmetic oracle and the C++ functional model. It also
checks zero, ones, ties, wide/tiny scales and reserved NaN scales for every
format. It requires both seeds to have identical transfer traces and latency,
and checks the existing MXFP4 records for regressions. The cast test separately
checks every narrow-format midpoint, its adjacent FP32 values, both signs,
special values and random bit patterns against enumerated representable values.

Build the production cluster and the isolated transaction regression:

```sh
export USE_GVRUN=1 CCACHE_DISABLE=1
export PATH="$PWD/install/bin:$PATH"
export PYTHONPATH="$PWD/pulp:$PWD/install/python:$PYTHONPATH"
make TARGETS='mxcore_output_test;arche3d_dma_test' \
  MODULES="$PWD/scripts/mxcore_fp4;$PWD/pulp/tests/arche3d" \
  cfg="$PWD/arche3d_sdk/apps/collective_row_sweep/ram_config.py" build
python scripts/mxcore_fp4/validate.py
python scripts/mxcore_fp4/benchmark.py
```

The isolated test compares every address, direction, issue cycle, result byte,
and completion cycle to RTL for all 336 profiles, including delayed service,
asynchronous responses, denial/retry and memory errors. It also checks MMIO
exclusion and timing/traffic counters. `validate.py` runs those five modes and
the existing SDK accelerator regression, and records `validation.json`.
The benchmark uses the production
Arche3D cluster via the existing one-cluster software fixture, with the memory
backend instead of DRAMSys. All GEMM buffers are local L1, so the measured
matrix jobs do not access DRAM. Instructions execute through the production
core/cache paths; SDK launch/poll overhead is reported separately.

## Metrics

The benchmark runs three M×N×K shapes per active engine:

| M | N | K (shared dimension) | Useful MACs |
| --- | --- | --- | ---: |
| 32 | 32 | 576 | 589,824 |
| 32 | 64 | 192 | 393,216 |
| 32 | 192 | 64 | 393,216 |

Each shape runs with one and four active engines, all six output formats, and
three repetitions (108 scenario runs / 270 engine jobs). Peak is 1,024 MACs per
engine per cycle. The cluster runs at 1 GHz and has four engines in both
scenarios. The shared HWPE limit is 512 **bytes**/cycle, while each engine's
port transfers at most 32 bytes/cycle.

- Per-engine job time: trigger acceptance to completion, including L1 service.
- Concurrent runtime: earliest trigger to latest completion. Launch skew is
  reported; buffers are private, and owners 0–3 launch after a shared barrier.
- Per-engine MAC utilization: `M*N*K / (1024 * job_cycles)`.
- Overall physical-cluster MAC utilization:
  `active_count * M*N*K / (4 * 1024 * concurrent_runtime)`.
- Active-engine normalized utilization uses `active_count` in the denominator
  instead of four. The one-engine physical-cluster figure includes three idle
  engines. Busy coverage is a separate elapsed-time metric, not MAC utilization.
- Software window: core 0's timestamp before the launch barrier through the
  completion barrier, including runtime setup, polling and synchronization.
  Initialization, output verification, diagnostics and printing are outside it.
- Traffic counts are actual completed 32-byte transfers. Memory-stall cycles
  are the extra service cycles added to the nominal measured RTL schedule.

`benchmark.json` retains all three repetitions and each engine's counters,
buffer addresses, launch timestamps, and verification status. It also records
MAC utilization over the common window and the interval when all active engines
overlap. Each shape/output pair uses a separate ELF to fit the existing 32 KiB
single-cluster program stripe. Golden results use lossless byte-plane RLE and
are decoded into L1 before timing. All six formats use identical input values
for a given shape. Input and output fixtures must match the recorded RTL hashes.
Private buffers reserve the largest output (FP32) for that shape, so output
format comparisons keep buffer addresses consistent. The summary
chooses the run with median concurrent runtime and also includes the range.

**Timing scope:** no-stall single-engine timing is calibrated to RTL. Arche3D
contention is modeled by adding each memory-service delay to the remaining
trace. This preserves backpressure and ordering but does not reproduce all
possible RTL FIFO overlap during stalls. The four-engine results are GVSoC
measurements, not a calibration against a four-engine RTL cluster.
