#include "collective_innetwork.h"
#include "hardware_collective_innetwork_test_config.h"

#define INNETWORK_BYTES          4u
#define INNETWORK_BCAST_OFFSET  0x1000u
#define INNETWORK_REDUCE_SEND   0x1100u
#define INNETWORK_REDUCE_RECV   0x1200u
#define INNETWORK_SCATTER_SEND  0x2000u
#define INNETWORK_SCATTER_RECV  0x3000u
#define INNETWORK_GATHER_SEND   0x3100u
#define INNETWORK_GATHER_RECV   0x4000u
#define INNETWORK_ALLTOALL_SEND 0x5000u
#define INNETWORK_ALLTOALL_RECV 0x7000u
#define INNETWORK_STATUS_OFFSET 0x9000u

static inline volatile uint8_t *bytes_at(uint32_t offset)
{
    return (volatile uint8_t *)local(offset);
}

static inline uint8_t pattern(uint32_t tag, uint32_t a, uint32_t b)
{
    return (uint8_t)(0x21u + tag * 17u + a * 7u + b * 3u);
}

static uint32_t floor_log2_u32(uint32_t value)
{
    uint32_t result = 0;
    while ((1u << (result + 1u)) <= value)
    {
        result++;
    }
    return result;
}

static uint32_t capped_group_count(uint32_t requested, uint32_t cap)
{
    uint32_t count = requested;

    if (cap != 0 && count > cap)
    {
        count = cap;
    }
    if (count > ARCH_NUM_CLUSTER)
    {
        count = ARCH_NUM_CLUSTER;
    }
    if (count < 1)
    {
        count = 1;
    }

    return count;
}

static void fill_bytes(uint32_t offset, uint8_t value, uint32_t bytes)
{
    volatile uint8_t *data = bytes_at(offset);
    for (uint32_t i = 0; i < bytes; i++)
    {
        data[i] = value;
    }
}

static uint32_t check_bytes(uint32_t offset, uint8_t value, uint32_t bytes)
{
    volatile uint8_t *data = bytes_at(offset);
    for (uint32_t i = 0; i < bytes; i++)
    {
        if (data[i] != value)
        {
            return 1;
        }
    }
    return 0;
}

static uint8_t reduce_expected(uint32_t tag, uint32_t count, uint32_t byte)
{
    uint32_t sum = 0;
    for (uint32_t rank = 0; rank < count; rank++)
    {
        sum += pattern(tag, rank, byte);
    }
    return (uint8_t)sum;
}

static uint32_t run_group(
    const velocity_innetwork_group_t *group,
    uint32_t seq_base,
    uint32_t tag)
{
    uint32_t cid = flex_get_core_id();
    uint32_t count = collective_innetwork_group_count(group);
    int32_t rank = collective_innetwork_group_rank(group, cid);
    int32_t root_signed = collective_innetwork_group_member(group, 0);
    uint32_t root = (uint32_t)root_signed;
    uint32_t active = rank >= 0;
    uint32_t failed = 0;

    if (count == 0 || root_signed < 0)
    {
        return 1;
    }

    if (active)
    {
        fill_bytes(INNETWORK_BCAST_OFFSET, cid == root ? pattern(tag, 0, 0) : 0, INNETWORK_BYTES);
    }
    flex_barrier_all();
    if (active)
    {
        failed |= collective_innetwork_broadcast(
            group, root, INNETWORK_BCAST_OFFSET, INNETWORK_BYTES, seq_base + 0u);
    }
    flex_barrier_all();
    if (active)
    {
        failed |= check_bytes(INNETWORK_BCAST_OFFSET, pattern(tag, 0, 0), INNETWORK_BYTES);
    }
    flex_barrier_all();

    if (active)
    {
        volatile uint8_t *send = bytes_at(INNETWORK_REDUCE_SEND);
        volatile uint8_t *recv = bytes_at(INNETWORK_REDUCE_RECV);
        for (uint32_t i = 0; i < INNETWORK_BYTES; i++)
        {
            send[i] = pattern(tag + 1u, (uint32_t)rank, i);
            recv[i] = 0;
        }
    }
    flex_barrier_all();
    if (active)
    {
        failed |= collective_innetwork_reduce_int8_sum(
            group, root, INNETWORK_REDUCE_SEND, INNETWORK_REDUCE_RECV,
            INNETWORK_BYTES, seq_base + 1u);
    }
    flex_barrier_all();
    if (cid == root)
    {
        volatile uint8_t *recv = bytes_at(INNETWORK_REDUCE_RECV);
        for (uint32_t i = 0; i < INNETWORK_BYTES; i++)
        {
            if (recv[i] != reduce_expected(tag + 1u, count, i))
            {
                failed = 1;
            }
        }
    }
    flex_barrier_all();

    if (active)
    {
        fill_bytes(INNETWORK_SCATTER_RECV, 0, INNETWORK_BYTES);
        if (cid == root)
        {
            volatile uint8_t *send = bytes_at(INNETWORK_SCATTER_SEND);
            for (uint32_t r = 0; r < count; r++)
            {
                for (uint32_t i = 0; i < INNETWORK_BYTES; i++)
                {
                    send[r * INNETWORK_BYTES + i] = pattern(tag + 2u, r, i);
                }
            }
        }
    }
    flex_barrier_all();
    if (active)
    {
        failed |= collective_innetwork_scatter(
            group, root, INNETWORK_SCATTER_SEND, INNETWORK_SCATTER_RECV,
            INNETWORK_BYTES, seq_base + 2u);
    }
    flex_barrier_all();
    if (active)
    {
        volatile uint8_t *recv = bytes_at(INNETWORK_SCATTER_RECV);
        for (uint32_t i = 0; i < INNETWORK_BYTES; i++)
        {
            if (recv[i] != pattern(tag + 2u, (uint32_t)rank, i))
            {
                failed = 1;
            }
        }
    }
    flex_barrier_all();

    if (active)
    {
        volatile uint8_t *send = bytes_at(INNETWORK_GATHER_SEND);
        for (uint32_t i = 0; i < INNETWORK_BYTES; i++)
        {
            send[i] = pattern(tag + 3u, (uint32_t)rank, i);
        }
        if (cid == root)
        {
            fill_bytes(INNETWORK_GATHER_RECV, 0, count * INNETWORK_BYTES);
        }
    }
    flex_barrier_all();
    if (active)
    {
        failed |= collective_innetwork_gather(
            group, root, INNETWORK_GATHER_SEND, INNETWORK_GATHER_RECV,
            INNETWORK_BYTES, seq_base + 3u);
    }
    flex_barrier_all();
    if (cid == root)
    {
        volatile uint8_t *recv = bytes_at(INNETWORK_GATHER_RECV);
        for (uint32_t r = 0; r < count; r++)
        {
            for (uint32_t i = 0; i < INNETWORK_BYTES; i++)
            {
                if (recv[r * INNETWORK_BYTES + i] != pattern(tag + 3u, r, i))
                {
                    failed = 1;
                }
            }
        }
    }
    flex_barrier_all();

    if (active)
    {
        volatile uint8_t *send = bytes_at(INNETWORK_ALLTOALL_SEND);
        volatile uint8_t *recv = bytes_at(INNETWORK_ALLTOALL_RECV);
        for (uint32_t dst_rank = 0; dst_rank < count; dst_rank++)
        {
            for (uint32_t i = 0; i < INNETWORK_BYTES; i++)
            {
                send[dst_rank * INNETWORK_BYTES + i] =
                    pattern(tag + 4u, (uint32_t)rank, dst_rank + i);
                recv[dst_rank * INNETWORK_BYTES + i] = 0;
            }
        }
    }
    flex_barrier_all();
    if (active)
    {
        failed |= collective_innetwork_alltoall(
            group, INNETWORK_ALLTOALL_SEND, INNETWORK_ALLTOALL_RECV,
            INNETWORK_BYTES, seq_base + 4u);
    }
    flex_barrier_all();
    if (active)
    {
        volatile uint8_t *recv = bytes_at(INNETWORK_ALLTOALL_RECV);
        for (uint32_t src_rank = 0; src_rank < count; src_rank++)
        {
            for (uint32_t i = 0; i < INNETWORK_BYTES; i++)
            {
                if (recv[src_rank * INNETWORK_BYTES + i] !=
                    pattern(tag + 4u, src_rank, (uint32_t)rank + i))
                {
                    failed = 1;
                }
            }
        }
    }
    flex_barrier_all();

    return failed;
}

static uint32_t run_broadcast_group(
    const velocity_innetwork_group_t *group,
    uint32_t seq,
    uint32_t tag)
{
    uint32_t cid = flex_get_core_id();
    int32_t rank = collective_innetwork_group_rank(group, cid);
    int32_t root_signed = collective_innetwork_group_member(group, 0);
    uint32_t root = (uint32_t)root_signed;
    uint32_t active = rank >= 0;
    uint32_t failed = 0;

    if (root_signed < 0)
    {
        return 1;
    }

    if (active)
    {
        fill_bytes(INNETWORK_BCAST_OFFSET, cid == root ? pattern(tag, 0, 0) : 0, INNETWORK_BYTES);
    }
    flex_barrier_all();
    if (active)
    {
        failed |= collective_innetwork_broadcast(
            group, root, INNETWORK_BCAST_OFFSET, INNETWORK_BYTES, seq);
    }
    flex_barrier_all();
    if (active)
    {
        failed |= check_bytes(INNETWORK_BCAST_OFFSET, pattern(tag, 0, 0), INNETWORK_BYTES);
    }
    flex_barrier_all();

    return failed;
}

int main(void)
{
    uint32_t cid = flex_get_core_id();
    uint32_t failed = 0;
    uint32_t exhaustive_count = capped_group_count(
        ARCH_NUM_CLUSTER, HARDWARE_COLLECTIVE_INNETWORK_TEST_MAX_EXHAUSTIVE_GROUP);
    uint32_t pattern_count = capped_group_count(
        ARCH_NUM_CLUSTER, HARDWARE_COLLECTIVE_INNETWORK_TEST_MAX_PATTERN_GROUP);
    uint32_t small_count = exhaustive_count < 2u ? exhaustive_count : 2u;
    uint32_t power2_count = 1u << floor_log2_u32(pattern_count);
    uint32_t stride_count = capped_group_count(
        (ARCH_NUM_CLUSTER + 1u) / 2u, HARDWARE_COLLECTIVE_INNETWORK_TEST_MAX_PATTERN_GROUP);
    uint32_t stride_span = stride_count == 0 ? 0 : (stride_count - 1u) * 2u + 1u;
    uint32_t status_count = exhaustive_count > power2_count ? exhaustive_count : power2_count;
    status_count = status_count > stride_span ? status_count : stride_span;
    status_count = capped_group_count(status_count, 0);
    uint32_t nested_outer = stride_count;
    uint32_t nested_inner = 1u;
    velocity_innetwork_group_t group;

    if (exhaustive_count == ARCH_NUM_CLUSTER)
    {
        collective_innetwork_group_init_all(&group);
    }
    else
    {
        collective_innetwork_group_init_contiguous_range(&group, 0, exhaustive_count);
    }
    failed |= run_group(&group, 10u, 1u);

    collective_innetwork_group_init_contiguous_range(&group, 0, small_count);
    failed |= run_group(&group, 20u, 2u);

    collective_innetwork_group_init_power2_aligned_range(&group, 0, floor_log2_u32(power2_count));
    failed |= run_broadcast_group(&group, 30u, 3u);

    collective_innetwork_group_init_stride_clusters(&group, 0, stride_count, 2);
    failed |= run_broadcast_group(&group, 40u, 4u);

    collective_innetwork_group_init_nested_stride_clusters(
        &group, 0, nested_inner, 1, nested_outer, 2);
    failed |= run_broadcast_group(&group, 50u, 5u);

    if (status_count == ARCH_NUM_CLUSTER)
    {
        collective_innetwork_group_init_all(&group);
    }
    else
    {
        collective_innetwork_group_init_contiguous_range(&group, 0, status_count);
    }
    volatile uint8_t *status_send = bytes_at(INNETWORK_STATUS_OFFSET);
    volatile uint8_t *status_recv = bytes_at(INNETWORK_STATUS_OFFSET + 4u);
    status_send[0] = failed ? 1u : 0u;
    if (cid == 0)
    {
        status_recv[0] = 0;
    }
    flex_barrier_all();
    failed |= collective_innetwork_reduce_int8_sum(
        &group, 0, INNETWORK_STATUS_OFFSET, INNETWORK_STATUS_OFFSET + 4u, 1u, 60u);
    flex_barrier_all();
    if (cid == 0)
    {
        uint32_t total_failed = failed | status_recv[0];

        if (total_failed)
        {
            flex_print("HARDWARE_COLLECTIVE_INNETWORK_RESULT FAIL clusters=");
        }
        else
        {
            flex_print("HARDWARE_COLLECTIVE_INNETWORK_RESULT PASS clusters=");
        }
        flex_print_int(ARCH_NUM_CLUSTER);
        flex_print("\n");
        flex_eoc(total_failed);
    }

    return failed;
}
