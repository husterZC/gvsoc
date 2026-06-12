# Fat-Tree Unified Interconnect Tests

This test builds several fat-tree topology shapes, runs an all-to-all DMA write
pattern, validates the received data on every cluster, and records benchmark
numbers in `results/summary.csv`.

Shapes are described as `k<radix>_l<level>_c<clusters>`. `level` is the number
of switch/router stages in the generated folded fat-tree. For `level > 1`, the
generator splits router ports into `ceil(radix / 2)` downward ports and
`floor(radix / 2)` upward ports, so odd radices are valid.

From the repository root:

```bash
velocity/tests/unified_interco/fat_tree/run_fat_tree_topologies.sh
```

Run one or more named shapes:

```bash
velocity/tests/unified_interco/fat_tree/run_fat_tree_topologies.sh k4_l1_c4 k4_l3_c16
```

Available default shapes:

- `k4_l1_c4`: radix 4, level 1, 4 clusters.
- `k4_l2_c8`: radix 4, level 2, 8 clusters.
- `k4_l3_c16`: radix 4, level 3, 16 clusters.
- `k4_l4_c16`: radix 4, level 4, 16 clusters.
- `k5_l3_c15`: radix 5, level 3, 15 clusters.

Optional environment variables:

- `WORDS`: payload words per source-destination transfer, default `1`.
- `MAX_INFLIGHT`: per-cluster DMA requests kept in flight, default `8`.
- `PENDING_SIZE`: router/link pending buffer size in bytes, default `4096`.
- `TIMEOUT_SECONDS`: timeout per shape, default `1200`.

The benchmark line printed by the simulator has this form:

```text
FAT_TREE_ALL_TO_ALL_RESULT PASS clusters=8 radix=4 level=2 ... elapsed_ns=1234
```

The CSV reports `bandwidth_mb_s` as `bytes * 1000 / elapsed_ns`, where bytes
are payload bytes injected by all non-self source-destination transfers.
