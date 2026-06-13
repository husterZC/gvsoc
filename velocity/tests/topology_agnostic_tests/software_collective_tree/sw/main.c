#include "collective_tree.h"
#include "software_collective_tree_test_config.h"

#define COLLECTIVE_TEST_BYTES          16u
#define COLLECTIVE_TEST_RADIX          4u
#define COLLECTIVE_TEST_SCRATCH_STRIDE 64u

#define COLLECTIVE_TEST_BCAST_OFFSET       0x1000u
#define COLLECTIVE_TEST_REDUCE_OFFSET      0x1200u
#define COLLECTIVE_TEST_SCATTER_SEND       0x2000u
#define COLLECTIVE_TEST_SCATTER_RECV       0x3000u
#define COLLECTIVE_TEST_GATHER_SEND        0x3400u
#define COLLECTIVE_TEST_GATHER_RECV        0x4000u
#define COLLECTIVE_TEST_ALLTOALL_SEND      0x6000u
#define COLLECTIVE_TEST_ALLTOALL_RECV      0x8000u
#define COLLECTIVE_TEST_STATUS_OFFSET      0xa000u
#define COLLECTIVE_TEST_SCRATCH_OFFSET     0xb000u

static uint32_t all_clusters[ARCH_NUM_CLUSTER];
static uint32_t half_clusters[ARCH_NUM_CLUSTER];

static inline volatile uint8_t *test_bytes(uint32_t offset)
{
    return (volatile uint8_t *)local(offset);
}

static inline uint8_t bcast_pattern(uint32_t tag, uint32_t index)
{
    return (uint8_t)(0x11u + tag * 17u + index);
}

static inline uint8_t reduce_pattern(uint32_t tag, uint32_t rank, uint32_t index)
{
    return (uint8_t)(0x03u + tag * 5u + rank * 7u + index);
}

static inline uint8_t scatter_pattern(uint32_t tag, uint32_t rank, uint32_t index)
{
    return (uint8_t)(0x31u + tag * 11u + rank * 13u + index);
}

static inline uint8_t gather_pattern(uint32_t tag, uint32_t rank, uint32_t index)
{
    return (uint8_t)(0x51u + tag * 19u + rank * 5u + index);
}

static inline uint8_t alltoall_pattern(
    uint32_t tag,
    uint32_t src_rank,
    uint32_t dst_rank,
    uint32_t index)
{
    return (uint8_t)(0x71u + tag * 23u + src_rank * 9u + dst_rank * 3u + index);
}

static uint8_t reduce_expected(uint32_t tag, uint32_t size, uint32_t index)
{
    uint32_t sum = 0;

    for (uint32_t rank = 0; rank < size; rank++)
    {
        sum += reduce_pattern(tag, rank, index);
    }

    return (uint8_t)sum;
}

static void clear_block(uint32_t offset, uint32_t bytes)
{
    volatile uint8_t *data = test_bytes(offset);

    for (uint32_t i = 0; i < bytes; i++)
    {
        data[i] = 0;
    }
}

static uint32_t report_group_status(const collective_tree_group_t *group, uint32_t failed)
{
    volatile uint32_t *status = (volatile uint32_t *)local(COLLECTIVE_TEST_STATUS_OFFSET);

    if (!collective_tree_group_active(group))
    {
        return 0;
    }

    if (group->rank == 0)
    {
        uint32_t total_failed = failed;

        for (uint32_t rank = 1; rank < group->size; rank++)
        {
            *status = 0;
            total_failed |= velocity_dma_recv(
                group->clusters[rank], COLLECTIVE_TEST_STATUS_OFFSET, sizeof(uint32_t));
            total_failed |= *status;
        }

        return total_failed;
    }

    *status = failed;
    velocity_dma_send(group->clusters[0], COLLECTIVE_TEST_STATUS_OFFSET, sizeof(uint32_t));
    return failed;
}

static uint32_t run_broadcast_check(
    const collective_tree_group_t *group,
    uint32_t tag,
    uint32_t root_rank)
{
    volatile uint8_t *buffer = test_bytes(COLLECTIVE_TEST_BCAST_OFFSET);
    uint32_t failed = 0;
    uint32_t status;

    for (uint32_t i = 0; i < COLLECTIVE_TEST_BYTES; i++)
    {
        buffer[i] = group->rank == root_rank ? bcast_pattern(tag, i) : 0;
    }

    status = collective_tree_broadcast(group, root_rank, COLLECTIVE_TEST_BCAST_OFFSET,
        COLLECTIVE_TEST_BYTES);
    failed |= status;

    for (uint32_t i = 0; i < COLLECTIVE_TEST_BYTES; i++)
    {
        if (buffer[i] != bcast_pattern(tag, i))
        {
            failed = 1;
        }
    }

    return failed;
}

static uint32_t run_reduce_check(
    const collective_tree_group_t *group,
    uint32_t tag,
    uint32_t root_rank)
{
    volatile uint8_t *buffer = test_bytes(COLLECTIVE_TEST_REDUCE_OFFSET);
    uint32_t failed = 0;
    uint32_t status;

    for (uint32_t i = 0; i < COLLECTIVE_TEST_BYTES; i++)
    {
        buffer[i] = reduce_pattern(tag, group->rank, i);
    }

    status = collective_tree_reduce_int8_sum(group, root_rank, COLLECTIVE_TEST_REDUCE_OFFSET,
        COLLECTIVE_TEST_BYTES);
    failed |= status;

    if (group->rank == root_rank)
    {
        for (uint32_t i = 0; i < COLLECTIVE_TEST_BYTES; i++)
        {
            if (buffer[i] != reduce_expected(tag, group->size, i))
            {
                failed = 1;
            }
        }
    }

    return failed;
}

static uint32_t run_scatter_check(
    const collective_tree_group_t *group,
    uint32_t tag,
    uint32_t root_rank)
{
    volatile uint8_t *send = test_bytes(COLLECTIVE_TEST_SCATTER_SEND);
    volatile uint8_t *recv = test_bytes(COLLECTIVE_TEST_SCATTER_RECV);
    uint32_t failed = 0;
    uint32_t status;

    clear_block(COLLECTIVE_TEST_SCATTER_RECV, COLLECTIVE_TEST_BYTES);
    if (group->rank == root_rank)
    {
        for (uint32_t rank = 0; rank < group->size; rank++)
        {
            for (uint32_t i = 0; i < COLLECTIVE_TEST_BYTES; i++)
            {
                send[rank * COLLECTIVE_TEST_BYTES + i] = scatter_pattern(tag, rank, i);
            }
        }
    }

    status = collective_tree_scatter(group, root_rank, COLLECTIVE_TEST_SCATTER_SEND,
        COLLECTIVE_TEST_SCATTER_RECV, COLLECTIVE_TEST_BYTES);
    failed |= status;

    for (uint32_t i = 0; i < COLLECTIVE_TEST_BYTES; i++)
    {
        if (recv[i] != scatter_pattern(tag, group->rank, i))
        {
            failed = 1;
        }
    }

    return failed;
}

static uint32_t run_gather_check(
    const collective_tree_group_t *group,
    uint32_t tag,
    uint32_t root_rank)
{
    volatile uint8_t *send = test_bytes(COLLECTIVE_TEST_GATHER_SEND);
    volatile uint8_t *recv = test_bytes(COLLECTIVE_TEST_GATHER_RECV);
    uint32_t failed = 0;
    uint32_t status;

    for (uint32_t i = 0; i < COLLECTIVE_TEST_BYTES; i++)
    {
        send[i] = gather_pattern(tag, group->rank, i);
    }
    if (group->rank == root_rank)
    {
        clear_block(COLLECTIVE_TEST_GATHER_RECV, group->size * COLLECTIVE_TEST_BYTES);
    }

    status = collective_tree_gather(group, root_rank, COLLECTIVE_TEST_GATHER_SEND,
        COLLECTIVE_TEST_GATHER_RECV, COLLECTIVE_TEST_BYTES);
    failed |= status;

    if (group->rank == root_rank)
    {
        for (uint32_t rank = 0; rank < group->size; rank++)
        {
            for (uint32_t i = 0; i < COLLECTIVE_TEST_BYTES; i++)
            {
                if (recv[rank * COLLECTIVE_TEST_BYTES + i] != gather_pattern(tag, rank, i))
                {
                    failed = 1;
                }
            }
        }
    }

    return failed;
}

static uint32_t run_alltoall_check(const collective_tree_group_t *group, uint32_t tag)
{
    volatile uint8_t *send = test_bytes(COLLECTIVE_TEST_ALLTOALL_SEND);
    volatile uint8_t *recv = test_bytes(COLLECTIVE_TEST_ALLTOALL_RECV);
    uint32_t failed = 0;
    uint32_t status;

    for (uint32_t dst = 0; dst < group->size; dst++)
    {
        for (uint32_t i = 0; i < COLLECTIVE_TEST_BYTES; i++)
        {
            send[dst * COLLECTIVE_TEST_BYTES + i] =
                alltoall_pattern(tag, group->rank, dst, i);
            recv[dst * COLLECTIVE_TEST_BYTES + i] = 0;
        }
    }

    status = collective_tree_all_to_all(group, COLLECTIVE_TEST_ALLTOALL_SEND,
        COLLECTIVE_TEST_ALLTOALL_RECV, COLLECTIVE_TEST_BYTES);
    failed |= status;

    for (uint32_t src = 0; src < group->size; src++)
    {
        for (uint32_t i = 0; i < COLLECTIVE_TEST_BYTES; i++)
        {
            if (recv[src * COLLECTIVE_TEST_BYTES + i] !=
                alltoall_pattern(tag, src, group->rank, i))
            {
                failed = 1;
            }
        }
    }

    return failed;
}

static uint32_t run_collective_suite(
    const collective_tree_group_t *group,
    uint32_t tag,
    uint32_t root_rank)
{
    uint32_t failed = 0;

    if (!collective_tree_group_active(group))
    {
        return 0;
    }

    failed |= run_broadcast_check(group, tag, root_rank);
    failed |= run_reduce_check(group, tag, root_rank);
    failed |= run_scatter_check(group, tag, root_rank);
    failed |= run_gather_check(group, tag, root_rank);
    failed |= run_alltoall_check(group, tag);

    return report_group_status(group, failed);
}

static uint32_t run_collective_root_sweep(const collective_tree_group_t *group, uint32_t tag)
{
    uint32_t failed = run_collective_suite(group, tag, 0);

    if (group->size > 1)
    {
        failed |= run_collective_suite(group, tag + 1u, group->size - 1u);
    }

    return failed;
}

static uint32_t cap_group_size(uint32_t requested, uint32_t cap)
{
    uint32_t size = requested;

    if (cap != 0 && size > cap)
    {
        size = cap;
    }
    if (size < 2)
    {
        size = 2;
    }

    return size;
}

static uint32_t make_all_group(uint32_t *clusters)
{
    uint32_t size = cap_group_size(
        ARCH_NUM_CLUSTER, SOFTWARE_COLLECTIVE_TREE_TEST_MAX_ALL_GROUP);

    for (uint32_t rank = 0; rank < size; rank++)
    {
        clusters[rank] = rank;
    }

    return size;
}

static uint32_t make_half_group(uint32_t *clusters)
{
    uint32_t size = cap_group_size(
        ARCH_NUM_CLUSTER / 2u, SOFTWARE_COLLECTIVE_TREE_TEST_MAX_SUBSET_GROUP);

    if (size > ARCH_NUM_CLUSTER)
    {
        size = ARCH_NUM_CLUSTER;
    }

    for (uint32_t rank = 0; rank < size; rank++)
    {
        clusters[rank] = ARCH_NUM_CLUSTER >= size * 2u ? rank * 2u : rank;
    }

    return size;
}

static void print_result(
    uint32_t failed,
    uint32_t all_failed,
    uint32_t half_failed,
    uint32_t all_size,
    uint32_t half_size)
{
    if (failed)
    {
        flex_print("SOFTWARE_COLLECTIVE_TREE_RESULT FAIL ");
    }
    else
    {
        flex_print("SOFTWARE_COLLECTIVE_TREE_RESULT PASS ");
    }

    flex_print("clusters=");
    flex_print_int(ARCH_NUM_CLUSTER);
    flex_print(" radix=");
    flex_print_int(COLLECTIVE_TEST_RADIX);
    flex_print(" bytes=");
    flex_print_int(COLLECTIVE_TEST_BYTES);
    flex_print(" all_size=");
    flex_print_int(all_size);
    flex_print(" all_failed=");
    flex_print_int(all_failed);
    flex_print(" half_size=");
    flex_print_int(half_size);
    flex_print(" half_failed=");
    flex_print_int(half_failed);
    flex_print("\n");
}

int main(void)
{
    uint32_t cid = flex_get_core_id();
    uint32_t all_failed;
    uint32_t half_failed;
    uint32_t all_size;
    uint32_t half_size;
    collective_tree_group_t all_group;
    collective_tree_group_t half_group;

    if (ARCH_NUM_CLUSTER < 2)
    {
        if (cid == 0)
        {
            print_result(1, 1, 1, 0, 0);
            flex_eoc(1);
        }
        return 1;
    }

    all_size = make_all_group(all_clusters);
    half_size = make_half_group(half_clusters);

    collective_tree_group_init(&all_group, all_clusters, all_size,
        COLLECTIVE_TEST_RADIX, COLLECTIVE_TEST_SCRATCH_OFFSET,
        COLLECTIVE_TEST_SCRATCH_STRIDE, COLLECTIVE_TEST_RADIX);
    collective_tree_group_init(&half_group, half_clusters, half_size,
        COLLECTIVE_TEST_RADIX, COLLECTIVE_TEST_SCRATCH_OFFSET,
        COLLECTIVE_TEST_SCRATCH_STRIDE, COLLECTIVE_TEST_RADIX);

    all_failed = run_collective_root_sweep(&all_group, 0);
    half_failed = run_collective_root_sweep(&half_group, 2);

    if (cid == 0)
    {
        uint32_t failed = all_failed | half_failed;
        print_result(failed, all_failed, half_failed, all_size, half_size);
        flex_eoc(failed);
    }

    return all_failed | half_failed;
}
