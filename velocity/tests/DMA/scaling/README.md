# DMA Scaling Test

This test sweeps Velocity architectures with different `num_cluster` values and runs a DMA all-to-all transfer pattern.

## Run

From this directory or the repository root:

```sh
./velocity/tests/DMA/scaling/run_scaling.sh
```

To run a subset:

```sh
./velocity/tests/DMA/scaling/run_scaling.sh 2 8 32
```

Results are written to:

- `results/summary.csv`
- `results/logs/scaling_<clusters>.log`

## Architecture Variants

The `arch/` directory contains concrete `VelocityArch` files for:

```text
2, 4, 8, 16, 32, 64, 128 clusters
```

Each file keeps the default Velocity cluster parameters and only changes `num_cluster`.

