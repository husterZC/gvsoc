# Velocity Regression Matrix

The regression manager treats topology and test behavior as independent axes.
Test rows come from `velocity/tests/topology_agnostic_tests`, and topology
columns come from `topologies.json`.

```text
                         topology_a  topology_b  topology_c
read_after_write             PASS        PASS        FAIL
injection_rate               PASS        PASS        PASS
zero_load_latency            PASS        PASS        PASS
two_sided_test               PASS        PASS        PASS
software_collective_tree     PASS        PASS        PASS
max_bandwidth_4B             PASS        PASS        PASS
```

## Files

- `topologies.json`: topology-column name lists plus optional parameter overrides.
- `tests.json`: topology-agnostic test rows.
- `run_matrix.py`: Python matrix runner with colored row progress.
- `run_smoke.sh`: small matrix wrapper.
- `run_debug.sh`: single debug-case wrapper.
- `run_full.sh`: full matrix wrapper.

## Run

```bash
velocity/tests/regression/run_smoke.sh
velocity/tests/regression/run_debug.sh
velocity/tests/regression/run_full.sh
```

The debug manifest currently runs `max_bandwidth_4096B` on `fat_tree_K8_L3`
with `make rund`, which records DMA and unified interconnect traces in
`analyze_trace.txt` under the generated software build directory.

Subset examples:

```bash
python3 velocity/tests/regression/run_matrix.py --mode smoke --topology torus_2d_X3_Y2_wrap
python3 velocity/tests/regression/run_matrix.py --mode debug
python3 velocity/tests/regression/run_matrix.py --mode full --test read_after_write --test zero_load_latency
python3 velocity/tests/regression/run_matrix.py --mode smoke --run-target rund
```

## Topology Names

Topology columns are generated from their names. For normal cases, add the name
to `smoke`, `debug`, or `full`; no matching entry is needed in `topologies`.

Supported name forms:

```text
flat_C<num_cluster>
fat_tree_K<radix>_L<level>[_C<active_clusters>]
mesh_2d_X<x>_Y<y>[_C<active_clusters>]
mesh_3d_X<x>_Y<y>_Z<z>[_C<active_clusters>]
torus_2d_X<x>_Y<y>[_wrap|_tree|_minimal|_wrap_tree|_wrap_minimal][_C<active_clusters>]
torus_3d_X<x>_Y<y>_Z<z>[_wrap|_tree|_minimal|_wrap_tree|_wrap_minimal][_C<active_clusters>]
ruche_2d_X<x>_Y<y>_H<hop>[_C<active_clusters>]
ruche_3d_X<x>_Y<y>_Z<z>_H<hop>[_C<active_clusters>]
hexa_mesh_X<x>_Y<y>[_C<active_clusters>]
hexa_torus_X<x>_Y<y>[_wrap|_tree|_minimal|_wrap_tree|_wrap_minimal][_C<active_clusters>]
octa_mesh_X<x>_Y<y>[_C<active_clusters>]
octa_torus_X<x>_Y<y>[_wrap|_tree|_minimal|_wrap_tree|_wrap_minimal][_C<active_clusters>]
ring_N<size>[_wrap|_tree|_minimal|_wrap_tree|_wrap_minimal][_C<active_clusters>]
tree_R<radix>_L<level>[_C<active_clusters>]
dragonfly_G<groups>_A<routers_per_group>_P<terminals_per_router>[_C<active_clusters>]
hypercube_D<dims>[_C<active_clusters>]
```

If `_C...` is omitted, the runner uses the topology capacity as the active
cluster count. Add an entry to the optional `topologies` object only when a
generated topology needs an override, for example a custom comment or a
non-default arch attribute.

## Terminal Progress

The shell scripts handle environment setup, then the Python runner owns the
terminal UI. In an interactive terminal it renders one colored progress bar per
test type:

```text
[read_after_write     ] [ring_N4                     ] [################------------]  57% [PASS:4 | FAIL:0] RUN
[injection_rate       ] [ring_N4                     ] [################------------]  57% [PASS:4 | FAIL:0] RUN
[zero_load_latency    ] [ring_N4                     ] [################------------]  57% [PASS:4 | FAIL:0] RUN
[two_sided_test       ] [ring_N4                     ] [################------------]  57% [PASS:4 | FAIL:0] RUN
[software_collective_tree] [ring_N4                  ] [################------------]  57% [PASS:4 | FAIL:0] RUN
[max_bandwidth_4B     ] [ring_N4                     ] [################------------]  57% [PASS:4 | FAIL:0] RUN
```

Options:

```bash
python3 velocity/tests/regression/run_matrix.py --mode smoke --progress bar --color always
python3 velocity/tests/regression/run_matrix.py --mode smoke --progress line --color never
```

`JOBS=0` means one worker per selected test row. Set `JOBS=N` in the wrapper
environment to cap row workers. Debug mode always uses one worker so selected
test/topology cells execute serially. The wrappers also accept
`PROGRESS=auto|bar|line|off` and `COLOR=auto|always|never`.

Use `--run-target rund` to force traced simulator execution for any selected
matrix cells.

## Scheduling

Hardware is built once per topology because `make hw` installs the selected
model into the shared project tree. After each topology is built, the selected
test rows are launched in parallel with separate `sw_build_dir` values.

The actual `gvsoc` process is protected by a file lock by default because the
generated target and runtime install tree are shared. Use `--no-sim-lock` only
when running in an isolated environment.

## Outputs

```text
results/matrix/<mode>/<timestamp>/matrix.csv
results/matrix/<mode>/<timestamp>/detail.csv
results/matrix/<mode>/<timestamp>/logs/
```

## Environment

If Python, Conda, toolchain, or simulator setup fails, use the normal setup flow
in the repository root `README.md` and the troubleshooting notes in
`codex_notes.md`.
