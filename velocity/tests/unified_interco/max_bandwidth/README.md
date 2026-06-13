# Max-Bandwidth Unified Interconnect Test

This benchmark measures one-way DMA write bandwidth from cluster 0 to the last
cluster, while sweeping the DMA transaction payload size.

The timed region contains only the repeated data writes from source to
destination. The destination cluster validates the received buffer and reports
status back to cluster 0 after timing has stopped.

## Run

From the repository root:

```sh
velocity/tests/unified_interco/max_bandwidth/run_max_bandwidth.sh
```

Run one or more named default fat-tree shapes:

```sh
velocity/tests/unified_interco/max_bandwidth/run_max_bandwidth.sh k4_l2_c8 k4_l3_c16
```

Run against a specific architecture file:

```sh
CFG=/path/to/velocity_arch.py velocity/tests/unified_interco/max_bandwidth/run_max_bandwidth.sh
```

## Sweep Controls

Optional environment variables:

- `SIZES`: space-separated transaction sizes in bytes, default `4 16 64 256 1024 4096 16384 65536`. Sizes must be multiples of 4.
- `REPEAT`: number of DMA transactions per size, default `128`. Must be between 1 and 255 because DMA transaction ids are 8-bit and retained by the model.
- `MAX_INFLIGHT`: source-side DMA requests kept in flight, default `8`.
- `SLOTS`: number of source/destination buffer slots, default `MAX_INFLIGHT`.
- `PENDING_SIZE`: router/link pending queue size in bytes. By default the script picks a value large enough for the largest packet in the sweep.
- `DMA_BUFFER_SIZE`: generated DMA read/write buffer size in bytes. By default the script picks at least the largest transaction size.
- `TIMEOUT_SECONDS`: timeout per size, default `1200`.

Results are written to:

- `results/summary.csv`
- `results/logs/<shape>_size_<bytes>.log`

The CSV reports `bandwidth_mb_s` as `bytes * 1000 / elapsed_ns`.
