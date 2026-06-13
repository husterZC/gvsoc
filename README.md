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
directory.

## Project Layout

- `velocity/hw/`: Velocity hardware target, architecture files, DMA model, and
  cluster system integration.
- `velocity/hw/unified_interco/`: topology-agnostic unified router/link models
  plus topology builders in `topologies/`.
- `velocity/sw/`: default Velocity software application, runtime files, linker
  script, and CMake build entry point.
- `velocity/tests/topology_agnostic_tests/`: reusable software tests that can run
  against any supported interconnect topology.
- `velocity/tests/regression/`: topology/test matrix manifests, regression
  wrappers, colored progress runner, and result output.
- `velocity/utils/`: configuration generation helpers used by the build flow.
- `core/`, `gapy/`, `gvrun/`, `gvtest/`, `pulp/`, `pulpos/`: GVSOC framework,
  targets, launch tools, and PULP platform components.
- `third_party/`: external dependencies and toolchain installation area.
- `build/`, `install/`, `sw_build*/`: generated hardware, install, and software
  build outputs.

## Architecture And Topologies

The default architecture lives in
[velocity/hw/velocity_arch.py](velocity/hw/velocity_arch.py). Leaving
`self.unified_interco = None` keeps the legacy flat DMA interconnect. Set
`self.unified_interco` to a topology name, or set it to `True` with
`self.unified_interco_topology`, to instantiate the unified interconnect.

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
