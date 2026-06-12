# DMA Injection Rate Test

This test runs the DMA all-to-all pattern while sweeping a software injection gap between DMA submissions.

The script reports achieved injection rate and measured all-to-all latency:

- injection rate: `transfers / elapsed_time`
- latency: simulator time from synchronized test start to synchronized completion

## Run

```sh
./velocity/tests/DMA/injection_rate/run_injection_rate.sh
```

To use custom gaps:

```sh
./velocity/tests/DMA/injection_rate/run_injection_rate.sh 0 8 32 128
```

Optional environment variables:

- `CFG=/path/to/velocity_arch.py`: use a specific architecture file.
- `MAX_INFLIGHT=16`: change the software-side DMA submission window.
- `TIMEOUT_SECONDS=1200`: change per-run timeout.

Outputs:

- `results/logs/injection_gap_<gap>.log`
- `results/latency_vs_injection_rate.csv`
- `results/latency_vs_injection_rate.png`

