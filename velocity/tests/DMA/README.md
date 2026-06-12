# Velocity DMA Tests

This directory contains focused tests for the Velocity cluster DMA model.

## Layout

- `scaling/`: runs the DMA all-to-all software test across multiple architecture files with `num_cluster` from 2 to 128.
- `injection_rate/`: runs the all-to-all pattern while sweeping a software injection gap, then emits CSV data and a latency-vs-injection-rate plot.

Both tests assume the normal Velocity environment has been initialized from the repository root:

```sh
source init.sh
```

## Notes

- The software tests use `velocity_dma_*` runtime APIs.
- The all-to-all pattern writes one word from each source cluster into every destination cluster.
- The tests use `flex_barrier_all()` for virtual simulator-side synchronization between clusters.
- Test output and logs are written under each test's `results/` directory.

