// SPDX-License-Identifier: Apache-2.0
// One production Arche3D cluster, private L1 operands, owner cores 0..3.
#include "arche3d.h"
#include "fixtures.h"

typedef struct {
    uint8_t a[BENCH_M * BENCH_K / 2], b[BENCH_N * BENCH_K / 2];
    uint8_t sa[BENCH_M * BENCH_K / 32], sb[BENCH_N * BENCH_K / 32];
    uint8_t c[BENCH_M * BENCH_N * 4], sc[BENCH_M * BENCH_N / 32], guard[32];
} buffers_t;
static buffers_t buffers[4] __attribute__((aligned(512)));
static arche3d_mxcore_fp4_stats stats[4];
static unsigned launch[4], returned[4];
static uint8_t expected[EXPECTED_BYTES];

// Lossless byte-plane RLE keeps even the 32x192 FP32 golden result inside the
// existing 32 KiB program stripe. Decode once, before either timed scenario.
static int decode_expected(void) {
    unsigned src = 0, dst = 0;
    const unsigned plane_size = EXPECTED_BYTES / EXPECTED_PLANES;
    while (src < sizeof(expected_rle)) {
        unsigned token = expected_rle[src++];
        unsigned count = token < 128 ? token + 1 : (token & 127) + 3;
        if (count > EXPECTED_BYTES - dst || (token < 128 ? count : 1) > sizeof(expected_rle) - src)
            return 1;
        for (unsigned i = 0; i < count; ++i) {
            unsigned index = (dst % plane_size) * EXPECTED_PLANES + dst / plane_size;
            expected[index] = expected_rle[src + (token < 128 ? i : 0)];
            ++dst;
        }
        src += token < 128 ? count : 1;
    }
    return dst != EXPECTED_BYTES;
}

static void number(unsigned value) {
    char text[11];
    unsigned index = 10;
    text[index] = 0;
    do {
        text[--index] = '0' + value % 10;
        value /= 10;
    } while (value);
    arche3d_puts(text + index);
}
static void field(unsigned value) {
    arche3d_puts(",");
    number(value);
}

int main(void) {
    unsigned core = arche3d_core_id();
    if (arche3d_num_clusters() != 1 || arche3d_mxcore_fp4_count() != 4)
        return 1;
    if (core == 0 && decode_expected())
        return 11;
    if (core < 4) {
        buffers_t *b = &buffers[core];
        for (unsigned i = 0; i < sizeof(b->a); ++i)
            b->a[i] = input_a[i];
        for (unsigned i = 0; i < sizeof(b->b); ++i)
            b->b[i] = input_b[i];
        for (unsigned i = 0; i < sizeof(b->sa); ++i)
            b->sa[i] = input_sa[i];
        for (unsigned i = 0; i < sizeof(b->sb); ++i)
            b->sb[i] = input_sb[i];
        for (unsigned i = 0; i < 32; ++i)
            b->guard[i] = 0x5a;
    }
    arche3d_cluster_cores_barrier();
    // Keep both scenarios in the same four-engine hardware configuration.
    for (unsigned scenario = 0; scenario < 2; ++scenario) {
        unsigned active = scenario ? 4 : 1;
        // One format per ELF keeps its verified operands/results within the
        // single-cluster 32 KiB program stripe without changing the hardware.
        for (unsigned fmt = BENCH_FORMAT; fmt <= BENCH_FORMAT; ++fmt) {
            for (unsigned repeat = 0; repeat < 3; ++repeat) {
                if (core < active) {
                    for (unsigned i = 0; i < sizeof(buffers[core].c); ++i)
                        buffers[core].c[i] = 0xa5;
                    for (unsigned i = 0; i < sizeof(buffers[core].sc); ++i)
                        buffers[core].sc[i] = 0x5a;
                }
                arche3d_cluster_cores_barrier();
                unsigned begin = core == 0 ? (unsigned)arche3d_cycles() : 0;
                arche3d_cluster_cores_barrier();
                if (core < active) {
                    buffers_t *b = &buffers[core];
                    arche3d_mxcore_fp4_job job = {b->a,    b->b,    b->sa,
                                                  b->sb,   b->c,    fmt == 0 ? b->sc : NULL,
                                                  BENCH_M, BENCH_N, BENCH_K};
                    launch[core] = (unsigned)arche3d_cycles();
                    if (arche3d_mxcore_fp4_start_format(&job, (arche3d_mxcore_output_format)fmt))
                        return 2;
                    if (arche3d_mxcore_fp4_wait())
                        return 3;
                    returned[core] = (unsigned)arche3d_cycles();
                }
                // Non-owner/idle cores block in the hardware barrier. No DMA,
                // layout or Spatz workload runs inside the measured window.
                arche3d_cluster_cores_barrier();
                unsigned window = core == 0 ? (unsigned)arche3d_cycles() - begin : 0;
                if (core < active) {
                    buffers_t *b = &buffers[core];
                    if (arche3d_mxcore_fp4_get_stats(&stats[core]))
                        return 4;
                    if (stats[core].start_cycle >> 32)
                        return 5;
                    for (unsigned i = 0; i < OUTPUT_BYTES; ++i)
                        if (b->c[i] != expected[i])
                            return 6;
                    for (unsigned i = OUTPUT_BYTES; i < sizeof(b->c); ++i)
                        if (b->c[i] != 0xa5)
                            return 7;
                    for (unsigned i = 0; i < sizeof(b->sc); ++i)
                        if (b->sc[i] != (fmt == 0 ? expected[BENCH_M * BENCH_N / 2 + i] : 0x5a))
                            return 8;
                    for (unsigned i = 0; i < sizeof(b->guard); ++i) {
                        if (b->guard[i] != 0x5a)
                            return 9;
                    }
                    // An invalid format is rejected before acquisition.
                    arche3d_mxcore_fp4_job job = {b->a,  b->b,    b->sa,   b->sb,  b->c,
                                                  b->sc, BENCH_M, BENCH_N, BENCH_K};
                    if (arche3d_mxcore_fp4_start_format(&job, (arche3d_mxcore_output_format)6) !=
                        ARCHE3D_ACCEL_INVALID)
                        return 10;
                }
                arche3d_cluster_cores_barrier();
                if (core == 0) {
                    for (unsigned i = 0; i < active; ++i) {
                        arche3d_puts("MX_BENCH");
                        field(BENCH_M);
                        field(BENCH_N);
                        field(BENCH_K);
                        field(active);
                        field(fmt);
                        field(repeat);
                        field(i);
                        field((unsigned)stats[i].start_cycle);
                        field(stats[i].cycles);
                        field(stats[i].nominal_cycles);
                        field(stats[i].stall_cycles);
                        field(stats[i].read_bytes);
                        field(stats[i].write_bytes);
                        field((uintptr_t)buffers[i].a);
                        field((uintptr_t)buffers[i].c);
                        field(window);
                        field(launch[i]);
                        field(returned[i]);
                        arche3d_puts("\n");
                    }
                }
                arche3d_cluster_cores_barrier();
            }
        }
    }
    if (core == 0)
        arche3d_puts("MX_BENCH_PASS\n");
    return 0;
}
