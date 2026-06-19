# Velocity

Velocity is a GVSOC simulation target for a multi-cluster accelerator system.
This repository contains the GVSOC framework, the Velocity hardware model,
software build flow, unified interconnect topology models, and regression tests
used to validate software behavior across interconnect configurations.

Codex-specific setup notes, sandbox workarounds, and troubleshooting recipes
were moved to [codex_notes.md](codex_notes.md).

## Quick Start

Use `bash` from the repository root:

```bash
eval "$(/usr/local/anaconda3/bin/conda shell.bash hook)"
export CCACHE_DIR="$PWD/.ccache"
source init.sh
make hw sw run
```

A successful run builds the Velocity GVSOC target, builds the default software
image, and runs `sw_build/velocity.elf` on
`pulp.chips.velocity.velocity_target`.

## Common Commands

```bash
make hw
make sw
make run
make rund
make runv
make flowviz
make clean_sw
```

Useful variants:

```bash
make hw cfg=velocity/hw/velocity_arch.py
make sw app=velocity/tests/topology_agnostic_tests/max_bandwidth/sw
make sw sw_build_dir=sw_build_max_bandwidth app=velocity/tests/topology_agnostic_tests/max_bandwidth/sw
```

`make hw` copies the selected architecture into the generated Velocity target
tree and builds the simulator model. `make sw` builds the selected software
application. `make run` launches the simulator with the selected software build
directory. `make rund` enables detailed debug traces. `make runv` enables the
lower-volume packet flow traces used by the visualizer and writes
`packet_flow_trace.txt` into the selected software build directory. `make
flowviz` starts the browser visualizer for that trace.

## Project Layout

- `velocity/hw/`: Velocity hardware target, architecture files, DMA model, and
  cluster system integration.
- `velocity/hw/unified_interco/`: topology-agnostic unified router/link models
  plus topology builders in `topologies/`.
- `velocity/tools/`: runnable Velocity helper tools, including configuration
  generation and the browser-based packet flow visualizer for `runv` traces.
  See [velocity/tools/README.md](velocity/tools/README.md) for organization.
- `velocity/sw/`: default Velocity software application, runtime files, linker
  script, and CMake build entry point.
- `velocity/tests/topology_agnostic_tests/`: reusable software tests that can run
  against any supported interconnect topology.
- `velocity/tests/regression/`: topology/test matrix manifests, regression
  wrappers, colored progress runner, and result output.
- `core/`, `gapy/`, `gvrun/`, `gvtest/`, `pulp/`, `pulpos/`: GVSOC framework,
  targets, launch tools, and PULP platform components.
- `third_party/`: external dependencies and toolchain installation area.
- `build/`, `install/`, `sw_build*/`: generated hardware, install, and software
  build outputs.

## Architecture And Topologies

The default architecture lives in
[velocity/hw/velocity_arch.py](velocity/hw/velocity_arch.py). Velocity has
separate on-chip and off-chip interconnect configuration:

- `num_chip`: number of Velocity chips in the platform.
- `num_cluster`: clusters per chip.
- `onchip`: topology for cluster-to-cluster traffic inside each chip.
- `offchip`: topology for chip-to-chip RDMA traffic.

Each chip has its own on-chip unified interconnect. The platform-level off-chip
interconnect also uses the unified router/link building blocks, with each chip's
cluster 0 RDMA endpoint attached as the chip-to-chip initiator/receiver.

Topology implementations live in
[velocity/hw/unified_interco/topologies/](velocity/hw/unified_interco/topologies/).
The supported families include:

- `fat_tree`
- `mesh_2d`, `mesh_3d`
- `torus_2d`, `torus_3d`
- `ruche_2d`, `ruche_3d`
- `hexa_mesh`, `hexa_torus`
- `octa_mesh`, `octa_torus`
- `ring`
- `tree`
- `dragonfly`
- `hypercube`

Each topology builder checks its parameters and supports partial population when
`num_cluster` is less than or equal to the topology endpoint capacity. See
[velocity/hw/unified_interco/topologies/README.md](velocity/hw/unified_interco/topologies/README.md)
for topology parameters and routing modes.

## Packet Flow Traces And Visualization

Use `make runv` to collect packet-level flow traces without enabling the full
debug trace stream:

```bash
make hw
make sw
make runv
```

The trace is written to:

```text
sw_build/packet_flow_trace.txt
```

For the multi-chip RDMA regression case, the regression runner can build the
generated multi-chip architecture and collect a `runv` trace in one step:

```bash
python3 velocity/tests/regression/run_matrix.py \
  --mode smoke \
  --test multi_chip_rdma \
  --run-target runv \
  --results tmp/flowviz_regression
```

Start the browser visualizer:

```bash
make flowviz
```

Then open:

```text
http://127.0.0.1:8765/
```

If you set `flowviz_port`, use that port instead. By default the browser opens
with empty arch and trace fields plus a file picker rooted at `velocity/`, so
you can choose or switch inputs without restarting FlowViz. The visualizer draws
chips, clusters, and the configured on-chip/off-chip routers and links from
`arch.py`, then overlays the packet movement observed in the trace. It supports
play/pause, timeline scrubbing, speed control, zoom/pan, filters, and packet
hover details. See [velocity/tools/flowviz/README.md](velocity/tools/flowviz/README.md)
for details and JSON export commands.

For a regression-generated multi-chip run, either choose the files in the
browser or preload that cell's architecture and trace:

```bash
ARCH=$(find tmp/flowviz_regression -name 'velocity_arch_multi_chip_ring_N2_C4.py' | head -n 1)
TRACE=$(find tmp/flowviz_regression -name packet_flow_trace.txt | head -n 1)
make flowviz flowviz_arch="$ARCH" flowviz_trace="$TRACE"
```

## Running Tests

The regression flow treats tests and topologies as independent axes: each test
row can be run against each topology column.

Run the small smoke matrix:

```bash
velocity/tests/regression/run_smoke.sh
```

Run the single debug case:

```bash
velocity/tests/regression/run_debug.sh
```

The debug case uses `make rund`, so simulator traces are also written to
`analyze_trace.txt` inside that cell's software build directory.

Run the full matrix:

```bash
velocity/tests/regression/run_full.sh
```

Common overrides:

```bash
JOBS=2 TIMEOUT=7200 velocity/tests/regression/run_smoke.sh
PROGRESS=bar COLOR=always velocity/tests/regression/run_smoke.sh
PROGRESS=off COLOR=never velocity/tests/regression/run_smoke.sh
```

Direct runner examples:

```bash
python3 velocity/tests/regression/run_matrix.py --mode smoke
python3 velocity/tests/regression/run_matrix.py --mode debug
python3 velocity/tests/regression/run_matrix.py --mode full --test zero_load_latency
python3 velocity/tests/regression/run_matrix.py --mode smoke --topology mesh_2d_X2_Y2
python3 velocity/tests/regression/run_matrix.py --mode smoke --run-target rund
python3 velocity/tests/regression/run_matrix.py --mode smoke --test multi_chip_rdma --run-target runv
```

Results are written under:

```text
velocity/tests/regression/results/matrix/<mode>/<timestamp>/
```

The important files are `matrix.csv`, `detail.csv`, and the per-cell logs in
`logs/`. See [velocity/tests/README.md](velocity/tests/README.md) and
[velocity/tests/regression/README.md](velocity/tests/regression/README.md) for
the full test organization and regression matrix syntax.

## Adding Tests Or Topologies

Add topology-agnostic software workloads under
`velocity/tests/topology_agnostic_tests/<test_name>/sw`, then register the row
in `velocity/tests/regression/tests.json`.

For normal generated topology cases, add only the topology name to
`velocity/tests/regression/topologies.json`. Names encode parameters, for
example:

```text
fat_tree_K4_L1
mesh_2d_X8_Y8
torus_2d_X4_Y4_wrap
ruche_3d_X4_Y2_Z2_H2
dragonfly_G2_A2_P1
hypercube_D7_C64
```

Use the optional `topologies` object in `topologies.json` only when a generated
case needs an explicit override.

## Environment And Troubleshooting

The normal setup path is:

```bash
eval "$(/usr/local/anaconda3/bin/conda shell.bash hook)"
export CCACHE_DIR="$PWD/.ccache"
source init.sh
```

If Python, Conda, toolchain, network, ccache, or Codex sandbox issues appear,
use [codex_notes.md](codex_notes.md). It contains the previous root README with
the detailed environment fixes.
