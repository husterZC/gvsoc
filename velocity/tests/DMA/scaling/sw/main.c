#include "velocity_runtime.h"

#define DMA_TEST_RX_BASE       0x1000u
#define DMA_TEST_SEND_OFFSET   0x0800u
#define DMA_TEST_MAX_INFLIGHT  8u

static inline uint32_t dma_test_pattern(uint32_t src)
{
    return 0x5a000000u | (src & 0x0000ffffu);
}

static void dma_test_delay(uint32_t cycles)
{
    for (volatile uint32_t i = 0; i < cycles; i++)
    {
        asm volatile("nop");
    }
}

int main(void)
{
    uint32_t cid = flex_get_core_id();
    volatile uint32_t *rx = (volatile uint32_t *)local(DMA_TEST_RX_BASE);
    volatile uint32_t *send_word = (volatile uint32_t *)local(DMA_TEST_SEND_OFFSET);
    uint32_t ids[DMA_TEST_MAX_INFLIGHT];
    uint32_t pending = 0;
    uint32_t failed = 0;

    for (uint32_t i = 0; i < ARCH_NUM_CLUSTER; i++)
    {
        rx[i] = 0;
    }
    rx[cid] = dma_test_pattern(cid);
    *send_word = dma_test_pattern(cid);

    flex_barrier_all();

    for (uint32_t dst = 0; dst < ARCH_NUM_CLUSTER; dst++)
    {
        if (dst == cid)
        {
            continue;
        }

        ids[pending++] = velocity_dma_write(
            dst,
            DMA_TEST_SEND_OFFSET,
            DMA_TEST_RX_BASE + cid * sizeof(uint32_t),
            sizeof(uint32_t),
            0);

        dma_test_delay(0);

        if (pending == DMA_TEST_MAX_INFLIGHT)
        {
            for (uint32_t i = 0; i < pending; i++)
            {
                velocity_dma_wait(ids[i]);
                if (velocity_dma_status(ids[i]) != VELOCITY_DMA_STATUS_DONE)
                {
                    failed = 1;
                }
            }
            pending = 0;
        }
    }

    for (uint32_t i = 0; i < pending; i++)
    {
        velocity_dma_wait(ids[i]);
        if (velocity_dma_status(ids[i]) != VELOCITY_DMA_STATUS_DONE)
        {
            failed = 1;
        }
    }

    flex_barrier_all();

    for (uint32_t src = 0; src < ARCH_NUM_CLUSTER; src++)
    {
        if (rx[src] != dma_test_pattern(src))
        {
            failed = 1;
        }
    }

    if (failed)
    {
        flex_print("DMA_SCALING_FAIL cluster=");
        flex_print_int(cid);
        flex_print(" error=");
        flex_print_int(velocity_dma_error());
        flex_print("\n");
    }

    flex_barrier_all();

    if (cid == 0)
    {
        if (failed)
        {
            flex_print("DMA_SCALING_RESULT FAIL clusters=");
        }
        else
        {
            flex_print("DMA_SCALING_RESULT PASS clusters=");
        }
        flex_print_int(ARCH_NUM_CLUSTER);
        flex_print("\n");
        flex_eoc(failed);
    }

    return failed;
}
