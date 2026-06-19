#ifndef EXAMPLE_COLLETIVE_INNETWORK_H
#define EXAMPLE_COLLETIVE_INNETWORK_H

#include "velocity_runtime.h"
#include "collective_innetwork.h"

/*
 * Baby examples for in-router collectives.
 *
 * Each example uses node 0 as the root. Nodes that are not part of a
 * partial group only join the global barriers, while active group members call
 * the collective API.
 */

#define EXAMPLE_INNETWORK_BYTES         4u
#define EXAMPLE_INNETWORK_BCAST_OFFSET 0x1000u
#define EXAMPLE_INNETWORK_REDUCE_SEND  0x1100u
#define EXAMPLE_INNETWORK_REDUCE_RECV  0x1200u
#define EXAMPLE_INNETWORK_SCATTER_SEND 0x2000u
#define EXAMPLE_INNETWORK_SCATTER_RECV 0x3000u
#define EXAMPLE_INNETWORK_GATHER_SEND  0x3100u
#define EXAMPLE_INNETWORK_GATHER_RECV  0x4000u
#define EXAMPLE_INNETWORK_STATUS_SEND  0x5000u
#define EXAMPLE_INNETWORK_STATUS_RECV  0x5010u

static inline volatile uint8_t *example_innetwork_bytes(uint32_t offset)
{
    return (volatile uint8_t *)(uintptr_t)local(offset);
}

static inline uint32_t example_innetwork_min(uint32_t a, uint32_t b)
{
    return a < b ? a : b;
}

static inline uint32_t example_innetwork_floor_log2(uint32_t value)
{
    uint32_t result = 0;

    while ((1u << (result + 1u)) <= value)
    {
        result++;
    }

    return result;
}

static inline uint8_t example_innetwork_pattern(uint32_t tag, uint32_t rank, uint32_t byte)
{
    return (uint8_t)(0x31u + tag * 13u + rank * 5u + byte);
}

static inline void example_innetwork_fill(uint32_t offset, uint8_t value, uint32_t bytes)
{
    volatile uint8_t *data = example_innetwork_bytes(offset);

    for (uint32_t i = 0; i < bytes; i++)
    {
        data[i] = value;
    }
}

static inline uint32_t example_innetwork_check(
    uint32_t offset,
    uint32_t tag,
    uint32_t rank,
    uint32_t bytes)
{
    volatile uint8_t *data = example_innetwork_bytes(offset);
    uint32_t failed = 0;

    for (uint32_t i = 0; i < bytes; i++)
    {
        if (data[i] != example_innetwork_pattern(tag, rank, i))
        {
            failed = 1;
        }
    }

    return failed;
}

static inline uint8_t example_innetwork_reduce_expected(
    uint32_t tag,
    uint32_t count,
    uint32_t byte)
{
    uint32_t sum = 0;

    for (uint32_t rank = 0; rank < count; rank++)
    {
        sum += example_innetwork_pattern(tag, rank, byte);
    }

    return (uint8_t)sum;
}

/*
 * Run four root-based examples on one logical group:
 *   1. node 0 broadcasts one buffer to the group
 *   2. node 0 receives the int8 sum of one buffer from every group member
 *   3. node 0 scatters one slice to each group member
 *   4. node 0 gathers one slice from each group member
 */
static inline uint32_t example_innetwork_run_group(
    const velocity_dma_port_t *dma,
    const velocity_innetwork_group_t *group,
    uint32_t seq_base,
    uint32_t tag)
{
    uint32_t cid = flex_get_core_id();
    uint32_t count = collective_innetwork_group_count(dma, group);
    int32_t rank_signed = collective_innetwork_group_rank(dma, group, cid);
    uint32_t active = rank_signed >= 0;
    uint32_t rank = (uint32_t)rank_signed;
    uint32_t failed = 0;

    if (count == 0 || collective_innetwork_group_member(dma, group, 0) != 0)
    {
        return 1;
    }

    /*
     * Broadcast: node 0 provides the bytes, and all group members receive
     * the same bytes at EXAMPLE_INNETWORK_BCAST_OFFSET.
     */
    if (active)
    {
        volatile uint8_t *data = example_innetwork_bytes(EXAMPLE_INNETWORK_BCAST_OFFSET);

        for (uint32_t i = 0; i < EXAMPLE_INNETWORK_BYTES; i++)
        {
            data[i] = cid == 0 ? example_innetwork_pattern(tag, 0, i) : 0;
        }
    }
    flex_barrier_all();
    if (active)
    {
        failed |= collective_innetwork_broadcast(
            dma, group, 0, EXAMPLE_INNETWORK_BCAST_OFFSET, EXAMPLE_INNETWORK_BYTES, seq_base);
    }
    flex_barrier_all();
    if (active)
    {
        failed |= example_innetwork_check(EXAMPLE_INNETWORK_BCAST_OFFSET, tag, 0, EXAMPLE_INNETWORK_BYTES);
    }
    flex_barrier_all();

    /*
     * Reduction: every group member contributes one int8 buffer. The router
     * sums bytes in-network and writes the final result only at node 0.
     */
    if (active)
    {
        volatile uint8_t *send = example_innetwork_bytes(EXAMPLE_INNETWORK_REDUCE_SEND);
        volatile uint8_t *recv = example_innetwork_bytes(EXAMPLE_INNETWORK_REDUCE_RECV);

        for (uint32_t i = 0; i < EXAMPLE_INNETWORK_BYTES; i++)
        {
            send[i] = example_innetwork_pattern(tag + 1u, rank, i);
            recv[i] = 0;
        }
    }
    flex_barrier_all();
    if (active)
    {
        failed |= collective_innetwork_reduce_int8_sum(
            dma, group, 0, EXAMPLE_INNETWORK_REDUCE_SEND, EXAMPLE_INNETWORK_REDUCE_RECV,
            EXAMPLE_INNETWORK_BYTES, seq_base + 1u);
    }
    flex_barrier_all();
    if (cid == 0)
    {
        volatile uint8_t *recv = example_innetwork_bytes(EXAMPLE_INNETWORK_REDUCE_RECV);

        for (uint32_t i = 0; i < EXAMPLE_INNETWORK_BYTES; i++)
        {
            if (recv[i] != example_innetwork_reduce_expected(tag + 1u, count, i))
            {
                failed = 1;
            }
        }
    }
    flex_barrier_all();

    /*
     * Scatter: node 0 stores count slices back-to-back. The in-router engine
     * sends slice rank N to group member rank N.
     */
    if (active)
    {
        example_innetwork_fill(EXAMPLE_INNETWORK_SCATTER_RECV, 0, EXAMPLE_INNETWORK_BYTES);
        if (cid == 0)
        {
            volatile uint8_t *send = example_innetwork_bytes(EXAMPLE_INNETWORK_SCATTER_SEND);

            for (uint32_t member = 0; member < count; member++)
            {
                for (uint32_t i = 0; i < EXAMPLE_INNETWORK_BYTES; i++)
                {
                    send[member * EXAMPLE_INNETWORK_BYTES + i] =
                        example_innetwork_pattern(tag + 2u, member, i);
                }
            }
        }
    }
    flex_barrier_all();
    if (active)
    {
        failed |= collective_innetwork_scatter(
            dma, group, 0, EXAMPLE_INNETWORK_SCATTER_SEND, EXAMPLE_INNETWORK_SCATTER_RECV,
            EXAMPLE_INNETWORK_BYTES, seq_base + 2u);
    }
    flex_barrier_all();
    if (active)
    {
        failed |= example_innetwork_check(
            EXAMPLE_INNETWORK_SCATTER_RECV, tag + 2u, rank, EXAMPLE_INNETWORK_BYTES);
    }
    flex_barrier_all();

    /*
     * Gather: every group member sends one slice. Node 0 receives all slices
     * in dense group-rank order.
     */
    if (active)
    {
        volatile uint8_t *send = example_innetwork_bytes(EXAMPLE_INNETWORK_GATHER_SEND);

        for (uint32_t i = 0; i < EXAMPLE_INNETWORK_BYTES; i++)
        {
            send[i] = example_innetwork_pattern(tag + 3u, rank, i);
        }
        if (cid == 0)
        {
            example_innetwork_fill(EXAMPLE_INNETWORK_GATHER_RECV, 0, count * EXAMPLE_INNETWORK_BYTES);
        }
    }
    flex_barrier_all();
    if (active)
    {
        failed |= collective_innetwork_gather(
            dma, group, 0, EXAMPLE_INNETWORK_GATHER_SEND, EXAMPLE_INNETWORK_GATHER_RECV,
            EXAMPLE_INNETWORK_BYTES, seq_base + 3u);
    }
    flex_barrier_all();
    if (cid == 0)
    {
        volatile uint8_t *recv = example_innetwork_bytes(EXAMPLE_INNETWORK_GATHER_RECV);

        for (uint32_t member = 0; member < count; member++)
        {
            for (uint32_t i = 0; i < EXAMPLE_INNETWORK_BYTES; i++)
            {
                if (recv[member * EXAMPLE_INNETWORK_BYTES + i] !=
                    example_innetwork_pattern(tag + 3u, member, i))
                {
                    failed = 1;
                }
            }
        }
    }
    flex_barrier_all();

    return failed;
}

static inline uint32_t example_innetwork_run(void)
{
    uint32_t cid = flex_get_core_id();
    uint32_t failed = 0;
    uint32_t small_count = example_innetwork_min(ARCH_NUM_CLUSTER, 4u);
    velocity_dma_port_t dma = flex_dma_port();
    velocity_innetwork_group_t group;

    /*
     * ALL: every node participates. Node 0 is rank 0 and the root.
     */
    collective_innetwork_group_init_all(&group);
    failed |= example_innetwork_run_group(&dma, &group, 10u, 1u);

    /*
     * CONTIGUOUS_RANGE: nodes [0, small_count) participate.
     */
    collective_innetwork_group_init_contiguous_range(&group, 0, small_count);
    failed |= example_innetwork_run_group(&dma, &group, 20u, 2u);

    /*
     * POWER2_ALIGNED_RANGE: nodes [0, 2^log2_size) participate.
     */
    collective_innetwork_group_init_power2_aligned_range(
        &group, 0, example_innetwork_floor_log2(small_count));
    failed |= example_innetwork_run_group(&dma, &group, 30u, 3u);

    /*
     * STRIDE: every other node, starting at node 0.
     */
    collective_innetwork_group_init_stride_nodes(
        &group, 0, example_innetwork_min((ARCH_NUM_CLUSTER + 1u) / 2u, 4u), 2u);
    failed |= example_innetwork_run_group(&dma, &group, 40u, 4u);

    /*
     * NESTED_STRIDE: two short inner groups separated by an outer stride.
     * On small systems, the descriptor naturally clips to live nodes.
     */
    collective_innetwork_group_init_nested_stride_nodes(
        &group,
        0,
        ARCH_NUM_CLUSTER > 1u ? 2u : 1u,
        1u,
        ARCH_NUM_CLUSTER > 2u ? 2u : 1u,
        ARCH_NUM_CLUSTER > 4u ? 4u : 2u);
    failed |= example_innetwork_run_group(&dma, &group, 50u, 5u);

    /*
     * Final status reduction over all nodes. This lets node 0 report a
     * single pass/fail result even if a non-root node detected an error.
     */
    collective_innetwork_group_init_all(&group);
    example_innetwork_bytes(EXAMPLE_INNETWORK_STATUS_SEND)[0] = failed ? 1u : 0u;
    if (cid == 0)
    {
        example_innetwork_bytes(EXAMPLE_INNETWORK_STATUS_RECV)[0] = 0;
    }
    flex_barrier_all();
    failed |= collective_innetwork_reduce_int8_sum(
        &dma, &group, 0, EXAMPLE_INNETWORK_STATUS_SEND, EXAMPLE_INNETWORK_STATUS_RECV, 1u, 60u);
    flex_barrier_all();

    if (cid == 0)
    {
        uint32_t total_failed = failed | example_innetwork_bytes(EXAMPLE_INNETWORK_STATUS_RECV)[0];

        if (total_failed)
        {
            flex_print("EXAMPLE_COLLETIVE_INNETWORK FAIL clusters=");
        }
        else
        {
            flex_print("EXAMPLE_COLLETIVE_INNETWORK PASS clusters=");
        }
        flex_print_int(ARCH_NUM_CLUSTER);
        flex_print("\n");
        flex_eoc(total_failed);
    }

    return failed;
}

#endif
