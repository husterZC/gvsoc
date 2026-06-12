# Fat-Tree Unified Interconnect Tests

This test builds several fat-tree topology shapes, runs an all-to-all DMA write
pattern, validates the received data on every cluster, and records benchmark
numbers in `results/summary.csv`.

From the repository root:

```bash
velocity/tests/unified_interco/fat_tree/run_fat_tree_topologies.sh
```

Run one or more named shapes:

```bash
velocity/tests/unified_interco/fat_tree/run_fat_tree_topologies.sh r4_c4_1pod r4_c8_2pod
```

Available default shapes:

- `r4_c4_1pod`: radix 4, 4 clusters, one pod.
- `r4_c8_2pod`: radix 4, 8 clusters, two pods.
- `r4_c16_4pod`: radix 4, 16 clusters, four pods.
- `r8_c16_1pod`: radix 8, 16 clusters, one pod.
- `r8_c32_2pod`: radix 8, 32 clusters, two pods.

Optional environment variables:

- `WORDS`: payload words per source-destination transfer, default `1`.
- `MAX_INFLIGHT`: per-cluster DMA requests kept in flight, default `8`.
- `PENDING_SIZE`: router/link pending buffer size in bytes, default `4096`.
- `TIMEOUT_SECONDS`: timeout per shape, default `1200`.

The benchmark line printed by the simulator has this form:

```text
FAT_TREE_ALL_TO_ALL_RESULT PASS clusters=8 radix=4 ... elapsed_ns=1234
```

The CSV reports `bandwidth_mb_s` as `bytes * 1000 / elapsed_ns`, where bytes
are payload bytes injected by all non-self source-destination transfers.
