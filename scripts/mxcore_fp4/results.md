# Arche3D MXCoreFP4 output benchmark

One production cluster at **1 GHz**, four installed engines, MXFP4 inputs. Every output byte and output canary passed. Each active engine executes one GEMM. Rows use the median-runtime repetition out of three. Runtime is earliest accelerator trigger through latest completion; setup is excluded.

## 32×32×576 (M×N×K; 589,824 useful MACs per engine)

| Active | Output | Runtime (ns) | Engine 0 / 1 / 2 / 3 MAC utilization (%) | Overall cluster (%) |
| --- | --- | ---: | --- | ---: |
| 1 | MXFP4 | 729 | 79.01 / 0.00 / 0.00 / 0.00 | 19.75 |
| 1 | FP32 | 834 | 69.06 / 0.00 / 0.00 / 0.00 | 17.27 |
| 1 | BF16 | 770 | 74.81 / 0.00 / 0.00 / 0.00 | 18.70 |
| 1 | FP16 | 770 | 74.81 / 0.00 / 0.00 / 0.00 | 18.70 |
| 1 | E4M3 | 738 | 78.05 / 0.00 / 0.00 / 0.00 | 19.51 |
| 1 | E5M2 | 738 | 78.05 / 0.00 / 0.00 / 0.00 | 19.51 |
| 4 | MXFP4 | 803 | 75.49 / 75.29 / 78.15 / 74.61 | 71.73 |
| 4 | FP32 | 899 | 66.98 / 66.90 / 68.57 / 66.21 | 64.07 |
| 4 | BF16 | 822 | 73.75 / 73.47 / 73.66 / 72.82 | 70.07 |
| 4 | FP16 | 828 | 73.10 / 72.54 / 72.18 / 72.27 | 69.57 |
| 4 | E4M3 | 790 | 76.90 / 76.60 / 76.80 / 75.89 | 72.91 |
| 4 | E5M2 | 790 | 76.90 / 76.60 / 76.80 / 75.89 | 72.91 |

| Active | Output | Job cycles, engines 0–3 | Extra memory cycles, engines 0–3 | Launch skew | Runtime range | SDK window cycles | Read+write BW (B/cycle) |
| --- | --- | --- | --- | ---: | --- | ---: | ---: |
| 1 | MXFP4 | 729 | 0 | 0 | 729–729 | 1281 | 27.61 |
| 1 | FP32 | 834 | 0 | 0 | 834–834 | 1319 | 28.39 |
| 1 | BF16 | 770 | 0 | 0 | 770–770 | 1263 | 28.09 |
| 1 | FP16 | 770 | 0 | 0 | 770–770 | 1263 | 28.09 |
| 1 | E4M3 | 738 | 0 | 0 | 738–738 | 1233 | 27.92 |
| 1 | E5M2 | 738 | 0 | 0 | 738–738 | 1233 | 27.92 |
| 4 | MXFP4 | 763/765/737/772 | 34/36/8/43 | 31 | 777–809 | 1359 | 100.26 |
| 4 | FP32 | 860/861/840/870 | 26/27/6/36 | 29 | 885–910 | 1389 | 105.36 |
| 4 | BF16 | 781/784/782/791 | 11/14/12/21 | 31 | 822–846 | 1318 | 105.27 |
| 4 | FP16 | 788/794/798/797 | 18/24/28/27 | 31 | 820–846 | 1327 | 104.50 |
| 4 | E4M3 | 749/752/750/759 | 11/14/12/21 | 31 | 787–803 | 1289 | 104.34 |
| 4 | E5M2 | 749/752/750/759 | 11/14/12/21 | 31 | 787–803 | 1289 | 104.34 |

| Output | Unstalled RTL cycles | Input read bytes/job | Output write bytes/job | Four-job throughput speedup |
| --- | ---: | ---: | ---: | ---: |
| MXFP4 | 729 | 19584 | 544 | 3.631× |
| FP32 | 834 | 19584 | 4096 | 3.711× |
| BF16 | 770 | 19584 | 2048 | 3.747× |
| FP16 | 770 | 19584 | 2048 | 3.720× |
| E4M3 | 738 | 19584 | 1024 | 3.737× |
| E5M2 | 738 | 19584 | 1024 | 3.737× |

## 32×64×192 (M×N×K; 393,216 useful MACs per engine)

| Active | Output | Runtime (ns) | Engine 0 / 1 / 2 / 3 MAC utilization (%) | Overall cluster (%) |
| --- | --- | ---: | --- | ---: |
| 1 | MXFP4 | 507 | 75.74 / 0.00 / 0.00 / 0.00 | 18.93 |
| 1 | FP32 | 718 | 53.48 / 0.00 / 0.00 / 0.00 | 13.37 |
| 1 | BF16 | 594 | 64.65 / 0.00 / 0.00 / 0.00 | 16.16 |
| 1 | FP16 | 594 | 64.65 / 0.00 / 0.00 / 0.00 | 16.16 |
| 1 | E4M3 | 532 | 72.18 / 0.00 / 0.00 / 0.00 | 18.05 |
| 1 | E5M2 | 532 | 72.18 / 0.00 / 0.00 / 0.00 | 18.05 |
| 4 | MXFP4 | 562 | 72.45 / 71.11 / 70.85 / 74.42 | 68.33 |
| 4 | FP32 | 780 | 50.93 / 51.41 / 50.93 / 50.73 | 49.23 |
| 4 | BF16 | 645 | 62.95 / 62.85 / 61.54 / 63.58 | 59.53 |
| 4 | FP16 | 665 | 62.34 / 61.34 / 61.05 / 60.57 | 57.74 |
| 4 | E4M3 | 583 | 71.51 / 70.46 / 69.82 / 69.57 | 65.87 |
| 4 | E5M2 | 583 | 71.51 / 70.46 / 69.82 / 69.57 | 65.87 |

| Active | Output | Job cycles, engines 0–3 | Extra memory cycles, engines 0–3 | Launch skew | Runtime range | SDK window cycles | Read+write BW (B/cycle) |
| --- | --- | --- | --- | ---: | --- | ---: | ---: |
| 1 | MXFP4 | 507 | 0 | 0 | 507–507 | 1054 | 27.90 |
| 1 | FP32 | 718 | 0 | 0 | 718–718 | 1201 | 29.59 |
| 1 | BF16 | 594 | 0 | 0 | 594–594 | 1081 | 28.88 |
| 1 | FP16 | 594 | 0 | 0 | 594–594 | 1081 | 28.88 |
| 1 | E4M3 | 532 | 0 | 0 | 532–532 | 1021 | 28.39 |
| 1 | E5M2 | 532 | 0 | 0 | 532–532 | 1021 | 28.39 |
| 4 | MXFP4 | 530/540/542/516 | 23/33/35/9 | 29 | 547–571 | 1115 | 100.67 |
| 4 | FP32 | 754/747/754/757 | 36/29/36/39 | 23 | 780–786 | 1276 | 108.96 |
| 4 | BF16 | 610/611/624/604 | 16/17/30/10 | 31 | 645–650 | 1138 | 106.37 |
| 4 | FP16 | 616/626/629/634 | 22/32/35/40 | 31 | 658–665 | 1157 | 103.17 |
| 4 | E4M3 | 537/545/550/552 | 5/13/18/20 | 31 | 572–583 | 1079 | 103.63 |
| 4 | E5M2 | 537/545/550/552 | 5/13/18/20 | 31 | 572–583 | 1079 | 103.63 |

| Output | Unstalled RTL cycles | Input read bytes/job | Output write bytes/job | Four-job throughput speedup |
| --- | ---: | ---: | ---: | ---: |
| MXFP4 | 507 | 13056 | 1088 | 3.609× |
| FP32 | 718 | 13056 | 8192 | 3.682× |
| BF16 | 594 | 13056 | 4096 | 3.684× |
| FP16 | 594 | 13056 | 4096 | 3.573× |
| E4M3 | 532 | 13056 | 2048 | 3.650× |
| E5M2 | 532 | 13056 | 2048 | 3.650× |

## 32×192×64 (M×N×K; 393,216 useful MACs per engine)

| Active | Output | Runtime (ns) | Engine 0 / 1 / 2 / 3 MAC utilization (%) | Overall cluster (%) |
| --- | --- | ---: | --- | ---: |
| 1 | MXFP4 | 544 | 70.59 / 0.00 / 0.00 / 0.00 | 17.65 |
| 1 | FP32 | 1200 | 32.00 / 0.00 / 0.00 / 0.00 | 8.00 |
| 1 | BF16 | 816 | 47.06 / 0.00 / 0.00 / 0.00 | 11.76 |
| 1 | FP16 | 816 | 47.06 / 0.00 / 0.00 / 0.00 | 11.76 |
| 1 | E4M3 | 624 | 61.54 / 0.00 / 0.00 / 0.00 | 15.38 |
| 1 | E5M2 | 624 | 61.54 / 0.00 / 0.00 / 0.00 | 15.38 |
| 4 | MXFP4 | 619 | 68.82 / 66.67 / 65.53 / 65.31 | 62.04 |
| 4 | FP32 | 1287 | 30.94 / 30.99 / 30.33 / 30.57 | 29.84 |
| 4 | BF16 | 904 | 45.55 / 44.65 / 45.50 / 43.99 | 42.48 |
| 4 | FP16 | 904 | 45.55 / 44.65 / 45.50 / 43.99 | 42.48 |
| 4 | E4M3 | 701 | 58.72 / 57.74 / 57.40 / 57.31 | 54.78 |
| 4 | E5M2 | 701 | 58.72 / 57.74 / 57.40 / 57.31 | 54.78 |

| Active | Output | Job cycles, engines 0–3 | Extra memory cycles, engines 0–3 | Launch skew | Runtime range | SDK window cycles | Read+write BW (B/cycle) |
| --- | --- | --- | --- | ---: | --- | ---: | ---: |
| 1 | MXFP4 | 544 | 0 | 0 | 544–544 | 1097 | 30.00 |
| 1 | FP32 | 1200 | 0 | 0 | 1200–1200 | 1687 | 31.36 |
| 1 | BF16 | 816 | 0 | 0 | 816–816 | 1310 | 31.06 |
| 1 | FP16 | 816 | 0 | 0 | 816–816 | 1310 | 31.06 |
| 1 | E4M3 | 624 | 0 | 0 | 624–624 | 1117 | 30.77 |
| 1 | E5M2 | 624 | 0 | 0 | 624–624 | 1117 | 30.77 |
| 4 | MXFP4 | 558/576/586/588 | 14/32/42/44 | 31 | 589–621 | 1177 | 105.46 |
| 4 | FP32 | 1241/1239/1266/1256 | 41/39/66/56 | 31 | 1280–1295 | 1781 | 116.96 |
| 4 | BF16 | 843/860/844/873 | 27/44/28/57 | 31 | 904–906 | 1404 | 112.14 |
| 4 | FP16 | 843/860/844/873 | 27/44/28/57 | 31 | 904–906 | 1404 | 112.14 |
| 4 | E4M3 | 654/665/669/670 | 30/41/45/46 | 31 | 699–713 | 1199 | 109.56 |
| 4 | E5M2 | 654/665/669/670 | 30/41/45/46 | 31 | 699–713 | 1199 | 109.56 |

| Output | Unstalled RTL cycles | Input read bytes/job | Output write bytes/job | Four-job throughput speedup |
| --- | ---: | ---: | ---: | ---: |
| MXFP4 | 544 | 13056 | 3264 | 3.515× |
| FP32 | 1200 | 13056 | 24576 | 3.730× |
| BF16 | 816 | 13056 | 12288 | 3.611× |
| FP16 | 816 | 13056 | 12288 | 3.611× |
| E4M3 | 624 | 13056 | 6144 | 3.561× |
| E5M2 | 624 | 13056 | 6144 | 3.561× |

## Interpretation

Per-engine utilization = `M*N*K / (1024 * engine_cycles)`. Overall cluster utilization = `active_count * M*N*K / (4 * 1024 * runtime_cycles)`. Idle engines count as zero. These measure useful MACs relative to peak, not just the fraction of time the engine is busy.

Both added shapes perform 393,216 MACs. The 32×192×64 case produces three times as many output elements as 32×64×192. Their input traffic includes the RTL streamer’s rereads of A for each N tile, so total matrix read traffic is 13,056 bytes per job for both shapes. Equal-width format pairs have identical unstalled RTL schedules; differences between their concurrent measurements reflect launch timing and arbitration state. Per-run ranges and per-engine memory-stall counts are included above.

The SDK window includes launch setup, polling and both barriers. Input initialization, output checks and printing occur outside it. Idle cores wait at a hardware barrier; DMA, layout and Spatz are idle. All jobs own private L1 buffers. Buffer sizes follow each shape, reserving FP32 output capacity for every output format of that shape. Exact addresses, source hashes and timestamps are recorded in [benchmark.json](benchmark.json). Each shape/output pair uses its own ELF within the existing 32 KiB program stripe. Golden results use lossless byte-plane RLE, decoded into L1 before timing. Inputs are identical across output formats for a given shape.

The HWPE fabric ceiling is 512 **bytes**/cycle shared by all engines; each MXCore has a 32-byte port with one outstanding request. The full JSON also includes throughput, active-engine normalized utilization and busy coverage.

**Accuracy scope:** unstalled single-engine schedules are RTL-calibrated. Concurrent L1 backpressure uses additive schedule stretching in GVSoC; these results have not been calibrated against a four-engine RTL cluster. The RAM backend replaces DRAMSys, and measured GEMMs access local L1 only. See [reproduction and metric definitions](README.md).
