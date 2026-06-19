#include "velocity_runtime.h"
#include "velocity_rdma.h"

#define RDMA_TEST_TX_OFFSET     0x3000u
#define RDMA_TEST_RX_OFFSET     0x3400u
#define RDMA_TEST_REMOTE_OFFSET 0x3800u

static inline uint32_t rdma_test_pattern(uint32_t dst_chip)
{
    return 0x6d430000u | (dst_chip & 0x0000ffffu);
}

static uint32_t rdma_test_wait_done(uint32_t txn_id)
{
    velocity_rdma_wait(txn_id);
    return velocity_rdma_status(txn_id) == VELOCITY_DMA_STATUS_DONE;
}

int main(void)
{
    uint32_t cid = flex_get_core_id();
    uint32_t chip_id = flex_chip_id();
    uint32_t failed = 0;

    if (cid != 0)
    {
        return 0;
    }

    if (chip_id != 0)
    {
        flex_eoc(0);
        return 0;
    }

    if (ARCH_NUM_CHIP < 2)
    {
        flex_print("MULTI_CHIP_RDMA_RESULT FAIL chips=");
        flex_print_int(ARCH_NUM_CHIP);
        flex_print("\n");
        flex_eoc(1);
        return 1;
    }

    volatile uint32_t *tx_word = (volatile uint32_t *)local(RDMA_TEST_TX_OFFSET);
    volatile uint32_t *rx_word = (volatile uint32_t *)local(RDMA_TEST_RX_OFFSET);

    for (uint32_t dst_chip = 1; dst_chip < ARCH_NUM_CHIP; dst_chip++)
    {
        uint32_t pattern = rdma_test_pattern(dst_chip);
        uint32_t txn_id;

        *tx_word = pattern;
        *rx_word = 0;

        txn_id = velocity_rdma_write(
            dst_chip,
            RDMA_TEST_TX_OFFSET,
            RDMA_TEST_REMOTE_OFFSET,
            sizeof(uint32_t),
            0);
        if (!rdma_test_wait_done(txn_id))
        {
            failed = 1;
        }

        txn_id = velocity_rdma_read(
            dst_chip,
            RDMA_TEST_RX_OFFSET,
            RDMA_TEST_REMOTE_OFFSET,
            sizeof(uint32_t),
            0);
        if (!rdma_test_wait_done(txn_id) || *rx_word != pattern)
        {
            failed = 1;
        }
    }

    if (failed)
    {
        flex_print("MULTI_CHIP_RDMA_RESULT FAIL chips=");
        flex_print_int(ARCH_NUM_CHIP);
        flex_print(" error=");
        flex_print_int(velocity_rdma_error());
        flex_print("\n");
    }
    else
    {
        flex_print("MULTI_CHIP_RDMA_RESULT PASS chips=");
        flex_print_int(ARCH_NUM_CHIP);
        flex_print("\n");
    }

    flex_eoc(failed);
    return failed;
}
