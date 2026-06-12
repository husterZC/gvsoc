# Zero-Load Topology Latency Test

This test sends one header-only DMA probe packet at a time from cluster 0 to
each other cluster. The source DMA stamps the cycle when the packet enters the
remote interconnect, and the destination DMA stamps the cycle when the packet
arrives at the destination DMA input.

From the repository root:

```bash
velocity/tests/unified_interco/zero_load_latency/run_zero_load_latency.sh
```

Run one or more default fat-tree shapes:

```bash
velocity/tests/unified_interco/zero_load_latency/run_zero_load_latency.sh r4_c4_1pod r4_c8_2pod
```

Run against a specific architecture file:

```bash
CFG=/path/to/velocity_arch.py velocity/tests/unified_interco/zero_load_latency/run_zero_load_latency.sh
```

Results are written to `results/summary.csv`, with one row per destination
cluster.
