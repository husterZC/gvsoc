#include "velocity_runtime.h"
#include "collective_innetwork.h"
#include "collective_tree.h"

#define RDMA_TEST_TX_OFFSET     0x3000u
#define RDMA_TEST_RX_OFFSET     0x3400u
#define RDMA_TEST_REMOTE_OFFSET 0x3800u

#define RDMA_TWO_SIDED_TX_OFFSET 0x3a00u
#define RDMA_TWO_SIDED_RX_OFFSET 0x3b00u

#define RDMA_INNETWORK_BYTES       4u
#define RDMA_INNETWORK_BCAST       0x3c00u
#define RDMA_INNETWORK_REDUCE_SEND 0x3d00u
#define RDMA_INNETWORK_REDUCE_RECV 0x3e00u

#define RDMA_TREE_BYTES          4u
#define RDMA_TREE_RADIX          2u
#define RDMA_TREE_BCAST          0x4000u
#define RDMA_TREE_REDUCE         0x4100u
#define RDMA_TREE_SCATTER_SEND   0x4200u
#define RDMA_TREE_SCATTER_RECV   0x4300u
#define RDMA_TREE_GATHER_SEND    0x4400u
#define RDMA_TREE_GATHER_RECV    0x4500u
#define RDMA_TREE_ALLTOALL_SEND  0x4600u
#define RDMA_TREE_ALLTOALL_RECV  0x4700u
#define RDMA_TREE_STATUS         0x4800u
#define RDMA_TREE_SCRATCH        0x4900u
#define RDMA_TREE_SCRATCH_STRIDE 64u

static uint32_t rdma_nodes[ARCH_NUM_CHIP];

static inline volatile uint8_t *rdma_bytes(uint32_t offset)
{
    return (volatile uint8_t *)(uintptr_t)local(offset);
}

static inline volatile uint32_t *rdma_word(uint32_t offset)
{
    return (volatile uint32_t *)(uintptr_t)local(offset);
}

static inline uint32_t rdma_test_pattern(uint32_t dst_chip)
{
    return 0x6d430000u | (dst_chip & 0x0000ffffu);
}

static inline uint32_t rdma_two_sided_pattern(
    uint32_t src_chip,
    uint32_t dst_chip,
    uint32_t step)
{
    return 0x74000000u |
        ((step & 0x000000ffu) << 16) |
        ((src_chip & 0x000000ffu) << 8) |
        (dst_chip & 0x000000ffu);
}

static inline uint8_t rdma_tree_pattern(uint32_t tag, uint32_t a, uint32_t b)
{
    return (uint8_t)(0x23u + tag * 19u + a * 7u + b * 3u);
}

static uint32_t rdma_wait_done(const velocity_dma_port_t *rdma, uint32_t txn_id)
{
    velocity_dma_wait(rdma, txn_id);
    return velocity_dma_status(rdma, txn_id) == VELOCITY_DMA_STATUS_DONE;
}

static uint32_t run_rdma_one_sided(const velocity_dma_port_t *rdma)
{
    volatile uint32_t *tx_word = rdma_word(RDMA_TEST_TX_OFFSET);
    volatile uint32_t *rx_word = rdma_word(RDMA_TEST_RX_OFFSET);
    uint32_t failed = 0;

    if (rdma->self_node != 0)
    {
        return 0;
    }

    for (uint32_t dst_chip = 1; dst_chip < ARCH_NUM_CHIP; dst_chip++)
    {
        uint32_t pattern = rdma_test_pattern(dst_chip);
        uint32_t txn_id;

        *tx_word = pattern;
        *rx_word = 0;

        txn_id = velocity_dma_write(
            rdma,
            dst_chip,
            RDMA_TEST_TX_OFFSET,
            RDMA_TEST_REMOTE_OFFSET,
            sizeof(uint32_t),
            0);
        if (!rdma_wait_done(rdma, txn_id))
        {
            failed = 1;
        }

        txn_id = velocity_dma_read(
            rdma,
            dst_chip,
            RDMA_TEST_RX_OFFSET,
            RDMA_TEST_REMOTE_OFFSET,
            sizeof(uint32_t),
            0);
        if (!rdma_wait_done(rdma, txn_id) || *rx_word != pattern)
        {
            failed = 1;
        }
    }

    return failed;
}

static uint32_t run_rdma_two_sided(const velocity_dma_port_t *rdma)
{
    volatile uint32_t *tx_word = rdma_word(RDMA_TWO_SIDED_TX_OFFSET);
    volatile uint32_t *rx_word = rdma_word(RDMA_TWO_SIDED_RX_OFFSET);
    uint32_t failed = 0;

    for (uint32_t peer = 1; peer < ARCH_NUM_CHIP; peer++)
    {
        if (rdma->self_node == 0)
        {
            *tx_word = rdma_two_sided_pattern(0, peer, 1);
            failed |= velocity_dma_send(
                rdma, peer, RDMA_TWO_SIDED_TX_OFFSET, sizeof(uint32_t));

            *rx_word = 0;
            failed |= velocity_dma_recv(
                rdma, peer, RDMA_TWO_SIDED_RX_OFFSET, sizeof(uint32_t));
            failed |= *rx_word != rdma_two_sided_pattern(peer, 0, 2);

            *tx_word = rdma_two_sided_pattern(0, peer, 3);
            *rx_word = 0;
            failed |= velocity_dma_sendrecv(
                rdma,
                peer,
                RDMA_TWO_SIDED_TX_OFFSET,
                RDMA_TWO_SIDED_RX_OFFSET,
                sizeof(uint32_t));
            failed |= *rx_word != rdma_two_sided_pattern(peer, 0, 3);
        }
        else if (rdma->self_node == peer)
        {
            *rx_word = 0;
            failed |= velocity_dma_recv(
                rdma, 0, RDMA_TWO_SIDED_RX_OFFSET, sizeof(uint32_t));
            failed |= *rx_word != rdma_two_sided_pattern(0, peer, 1);

            *tx_word = rdma_two_sided_pattern(peer, 0, 2);
            failed |= velocity_dma_send(
                rdma, 0, RDMA_TWO_SIDED_TX_OFFSET, sizeof(uint32_t));

            *tx_word = rdma_two_sided_pattern(peer, 0, 3);
            *rx_word = 0;
            failed |= velocity_dma_sendrecv(
                rdma,
                0,
                RDMA_TWO_SIDED_TX_OFFSET,
                RDMA_TWO_SIDED_RX_OFFSET,
                sizeof(uint32_t));
            failed |= *rx_word != rdma_two_sided_pattern(0, peer, 3);
        }
    }

    return failed;
}

static uint8_t rdma_reduce_expected(uint32_t count, uint32_t byte)
{
    uint32_t sum = 0;
    for (uint32_t node = 0; node < count; node++)
    {
        sum += rdma_tree_pattern(1u, node, byte);
    }
    return (uint8_t)sum;
}

static uint32_t run_rdma_innetwork(const velocity_dma_port_t *rdma)
{
    velocity_innetwork_group_t group = collective_innetwork_group_all();
    uint32_t count = collective_innetwork_group_count(rdma, &group);
    int32_t rank = collective_innetwork_group_rank(rdma, &group, rdma->self_node);
    uint32_t failed = 0;

    if (count != ARCH_NUM_CHIP || rank < 0)
    {
        return 1;
    }

    for (uint32_t i = 0; i < RDMA_INNETWORK_BYTES; i++)
    {
        rdma_bytes(RDMA_INNETWORK_BCAST)[i] =
            rdma->self_node == 0 ? rdma_tree_pattern(0u, 0u, i) : 0;
    }
    failed |= collective_innetwork_broadcast(
        rdma, &group, 0, RDMA_INNETWORK_BCAST, RDMA_INNETWORK_BYTES, 100u);
    for (uint32_t i = 0; i < RDMA_INNETWORK_BYTES; i++)
    {
        if (rdma_bytes(RDMA_INNETWORK_BCAST)[i] != rdma_tree_pattern(0u, 0u, i))
        {
            failed = 1;
        }
    }

    for (uint32_t i = 0; i < RDMA_INNETWORK_BYTES; i++)
    {
        rdma_bytes(RDMA_INNETWORK_REDUCE_SEND)[i] =
            rdma_tree_pattern(1u, rdma->self_node, i);
        rdma_bytes(RDMA_INNETWORK_REDUCE_RECV)[i] = 0;
    }
    failed |= collective_innetwork_reduce_int8_sum(
        rdma,
        &group,
        0,
        RDMA_INNETWORK_REDUCE_SEND,
        RDMA_INNETWORK_REDUCE_RECV,
        RDMA_INNETWORK_BYTES,
        101u);
    if (rdma->self_node == 0)
    {
        for (uint32_t i = 0; i < RDMA_INNETWORK_BYTES; i++)
        {
            if (rdma_bytes(RDMA_INNETWORK_REDUCE_RECV)[i] != rdma_reduce_expected(count, i))
            {
                failed = 1;
            }
        }
    }

    return failed;
}

static uint32_t run_rdma_tree(const velocity_dma_port_t *rdma)
{
    collective_tree_group_t group;
    uint32_t failed = 0;

    for (uint32_t node = 0; node < ARCH_NUM_CHIP; node++)
    {
        rdma_nodes[node] = node;
    }

    collective_tree_group_init(
        &group,
        rdma,
        rdma_nodes,
        ARCH_NUM_CHIP,
        RDMA_TREE_RADIX,
        RDMA_TREE_SCRATCH,
        RDMA_TREE_SCRATCH_STRIDE,
        RDMA_TREE_RADIX);

    if (!collective_tree_group_active(&group))
    {
        return 1;
    }

    for (uint32_t i = 0; i < RDMA_TREE_BYTES; i++)
    {
        rdma_bytes(RDMA_TREE_BCAST)[i] =
            group.rank == 0 ? rdma_tree_pattern(2u, 0u, i) : 0;
    }
    failed |= collective_tree_broadcast(&group, 0, RDMA_TREE_BCAST, RDMA_TREE_BYTES);
    for (uint32_t i = 0; i < RDMA_TREE_BYTES; i++)
    {
        if (rdma_bytes(RDMA_TREE_BCAST)[i] != rdma_tree_pattern(2u, 0u, i))
        {
            failed = 1;
        }
    }

    for (uint32_t i = 0; i < RDMA_TREE_BYTES; i++)
    {
        rdma_bytes(RDMA_TREE_REDUCE)[i] = rdma_tree_pattern(3u, group.rank, i);
    }
    failed |= collective_tree_reduce_int8_sum(&group, 0, RDMA_TREE_REDUCE, RDMA_TREE_BYTES);
    if (group.rank == 0)
    {
        for (uint32_t i = 0; i < RDMA_TREE_BYTES; i++)
        {
            uint32_t sum = 0;
            for (uint32_t rank = 0; rank < group.size; rank++)
            {
                sum += rdma_tree_pattern(3u, rank, i);
            }
            if (rdma_bytes(RDMA_TREE_REDUCE)[i] != (uint8_t)sum)
            {
                failed = 1;
            }
        }
    }

    if (group.rank == 0)
    {
        for (uint32_t rank = 0; rank < group.size; rank++)
        {
            for (uint32_t i = 0; i < RDMA_TREE_BYTES; i++)
            {
                rdma_bytes(RDMA_TREE_SCATTER_SEND)[rank * RDMA_TREE_BYTES + i] =
                    rdma_tree_pattern(4u, rank, i);
            }
        }
    }
    for (uint32_t i = 0; i < RDMA_TREE_BYTES; i++)
    {
        rdma_bytes(RDMA_TREE_SCATTER_RECV)[i] = 0;
    }
    failed |= collective_tree_scatter(
        &group, 0, RDMA_TREE_SCATTER_SEND, RDMA_TREE_SCATTER_RECV, RDMA_TREE_BYTES);
    for (uint32_t i = 0; i < RDMA_TREE_BYTES; i++)
    {
        if (rdma_bytes(RDMA_TREE_SCATTER_RECV)[i] != rdma_tree_pattern(4u, group.rank, i))
        {
            failed = 1;
        }
    }

    for (uint32_t i = 0; i < RDMA_TREE_BYTES; i++)
    {
        rdma_bytes(RDMA_TREE_GATHER_SEND)[i] = rdma_tree_pattern(5u, group.rank, i);
    }
    if (group.rank == 0)
    {
        for (uint32_t i = 0; i < group.size * RDMA_TREE_BYTES; i++)
        {
            rdma_bytes(RDMA_TREE_GATHER_RECV)[i] = 0;
        }
    }
    failed |= collective_tree_gather(
        &group, 0, RDMA_TREE_GATHER_SEND, RDMA_TREE_GATHER_RECV, RDMA_TREE_BYTES);
    if (group.rank == 0)
    {
        for (uint32_t rank = 0; rank < group.size; rank++)
        {
            for (uint32_t i = 0; i < RDMA_TREE_BYTES; i++)
            {
                if (rdma_bytes(RDMA_TREE_GATHER_RECV)[rank * RDMA_TREE_BYTES + i] !=
                    rdma_tree_pattern(5u, rank, i))
                {
                    failed = 1;
                }
            }
        }
    }

    for (uint32_t dst = 0; dst < group.size; dst++)
    {
        for (uint32_t i = 0; i < RDMA_TREE_BYTES; i++)
        {
            rdma_bytes(RDMA_TREE_ALLTOALL_SEND)[dst * RDMA_TREE_BYTES + i] =
                rdma_tree_pattern(6u, group.rank, dst * RDMA_TREE_BYTES + i);
            rdma_bytes(RDMA_TREE_ALLTOALL_RECV)[dst * RDMA_TREE_BYTES + i] = 0;
        }
    }
    failed |= collective_tree_all_to_all(
        &group, RDMA_TREE_ALLTOALL_SEND, RDMA_TREE_ALLTOALL_RECV, RDMA_TREE_BYTES);
    for (uint32_t src = 0; src < group.size; src++)
    {
        for (uint32_t i = 0; i < RDMA_TREE_BYTES; i++)
        {
            if (rdma_bytes(RDMA_TREE_ALLTOALL_RECV)[src * RDMA_TREE_BYTES + i] !=
                rdma_tree_pattern(6u, src, group.rank * RDMA_TREE_BYTES + i))
            {
                failed = 1;
            }
        }
    }

    return failed;
}

static uint32_t report_rdma_status(const velocity_dma_port_t *rdma, uint32_t failed)
{
    volatile uint32_t *status = rdma_word(RDMA_TREE_STATUS);

    if (rdma->self_node == 0)
    {
        uint32_t total_failed = failed;
        for (uint32_t node = 1; node < ARCH_NUM_CHIP; node++)
        {
            *status = 0;
            total_failed |= velocity_dma_recv(
                rdma, node, RDMA_TREE_STATUS, sizeof(uint32_t));
            total_failed |= *status;
        }
        return total_failed;
    }

    *status = failed;
    failed |= velocity_dma_send(rdma, 0, RDMA_TREE_STATUS, sizeof(uint32_t));
    return failed;
}

int main(void)
{
    uint32_t cid = flex_get_core_id();
    velocity_dma_port_t rdma;
    uint32_t failed = 0;
    uint32_t total_failed;

    if (cid != 0)
    {
        return 0;
    }

    rdma = flex_rdma_port();

    if (ARCH_NUM_CHIP < 2)
    {
        flex_print("MULTI_CHIP_RDMA_RESULT FAIL chips=");
        flex_print_int(ARCH_NUM_CHIP);
        flex_print("\n");
        flex_eoc(1);
        return 1;
    }

    failed |= run_rdma_one_sided(&rdma);
    failed |= run_rdma_two_sided(&rdma);
    failed |= run_rdma_innetwork(&rdma);
    failed |= run_rdma_tree(&rdma);

    total_failed = report_rdma_status(&rdma, failed);

    if (rdma.self_node == 0)
    {
        if (total_failed)
        {
            flex_print("MULTI_CHIP_RDMA_RESULT FAIL chips=");
            flex_print_int(ARCH_NUM_CHIP);
            flex_print(" error=");
            flex_print_int(velocity_dma_error(&rdma));
            flex_print("\n");
        }
        else
        {
            flex_print("MULTI_CHIP_RDMA_RESULT PASS chips=");
            flex_print_int(ARCH_NUM_CHIP);
            flex_print("\n");
        }
    }

    flex_eoc(total_failed);
    return total_failed;
}
