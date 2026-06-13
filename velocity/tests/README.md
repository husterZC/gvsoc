# Velocity Tests

Tests are organized around topology-agnostic software workloads. A regression
row is a test type, and a regression column is a topology configuration.

## Layout

- `topology_agnostic_tests/read_after_write/sw`: all clusters write to all other clusters, then validate received data.
- `topology_agnostic_tests/injection_rate/sw`: all-to-all traffic with an injection-gap control point.
- `topology_agnostic_tests/max_bandwidth/sw`: one-way DMA bandwidth transfer.
- `topology_agnostic_tests/zero_load_latency/sw`: one probe from cluster 0 to each other active cluster.
- `regression/`: topology/test manifests, shell entry points, and the Python matrix runner.

Only reusable software test apps live under `topology_agnostic_tests`. Add new
topology-independent behavior there first, then register it as a row in
`regression/tests.json`.

## Matrix Model

```text
                         mesh_2d_X2_Y2  torus_2d_X3_Y2_wrap  ring_N4
read_after_write              PASS              PASS           PASS
injection_rate                PASS              PASS           PASS
max_bandwidth_4B              PASS              PASS           PASS
zero_load_latency             PASS              PASS           PASS
```

Topology names include their parameter names, for example `fat_tree_K4_L1`,
`ruche_3d_X4_Y2_Z2_H2`, and `dragonfly_G2_A2_P1`. The regression runner
generates architecture parameters from these names, so adding a normal case like
`mesh_2d_X8_Y8` to `regression/topologies.json` is enough.

## Regression Entry Points

Run the small matrix:

```bash
velocity/tests/regression/run_smoke.sh
```

Run the full matrix:

```bash
velocity/tests/regression/run_full.sh
```

The shell wrappers set up the environment and call the Python matrix runner.
By default, the runner starts one worker per selected test row, so the terminal
shows one live progress bar per test type:

```text
[read_after_write     ] [mesh_2d_X2_Y2              ] [########--------------------]  28% [PASS:2 | FAIL:0] RUN
[injection_rate       ] [mesh_2d_X2_Y2              ] [########--------------------]  28% [PASS:2 | FAIL:0] RUN
[zero_load_latency    ] [mesh_2d_X2_Y2              ] [########--------------------]  28% [PASS:2 | FAIL:0] RUN
[max_bandwidth_4B     ] [mesh_2d_X2_Y2              ] [########--------------------]  28% [PASS:2 | FAIL:0] RUN
```

Override worker count or timeout when needed:

```bash
JOBS=2 TIMEOUT=7200 velocity/tests/regression/run_smoke.sh
PROGRESS=line COLOR=always velocity/tests/regression/run_smoke.sh
PROGRESS=off COLOR=never velocity/tests/regression/run_smoke.sh
```

Direct runner examples:

```bash
python3 velocity/tests/regression/run_matrix.py --mode smoke
python3 velocity/tests/regression/run_matrix.py --mode full --test zero_load_latency
python3 velocity/tests/regression/run_matrix.py --mode smoke --topology torus_2d_X3_Y2_wrap --color always
```

`gvsoc` invocations are locked by default because the generated target and
runtime install tree are shared inside the checkout. Software builds can still
run in parallel; simulator execution is serialized to avoid false failures.

## Results

```text
velocity/tests/regression/results/matrix/<mode>/<timestamp>/matrix.csv
velocity/tests/regression/results/matrix/<mode>/<timestamp>/detail.csv
velocity/tests/regression/results/matrix/<mode>/<timestamp>/logs/
```

`matrix.csv` is the compact row-by-column view. `detail.csv` contains one row
per cell with suite, status, return code, elapsed seconds, comment, and log path.

## Environment

Use the repository root README for environment setup. In particular, Codex or
cluster shells usually need the conda bash hook, `source init.sh`, and a
workspace-local `CCACHE_DIR`.
