#include "velocity_runtime.h"
#include "dma_test_config.h"

#define DMA_TEST_RX_BASE       0x1000u
#define DMA_TEST_SEND_OFFSET   0x0800u

static inline uint32_t dma_test_pattern(uint32_t src)
{
    return 0x6b000000u | (src & 0x0000ffffu);
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
    velocity_dma_port_t dma = flex_dma_port();

    for (uint32_t i = 0; i < ARCH_NUM_CLUSTER; i++)
    {
        rx[i] = 0;
    }
    rx[cid] = dma_test_pattern(cid);
    *send_word = dma_test_pattern(cid);

    if (cid == 0)
    {
        flex_timer_start();
    }
    flex_barrier_all();

    for (uint32_t dst = 0; dst < ARCH_NUM_CLUSTER; dst++)
    {
        if (dst == cid)
        {
            continue;
        }

        ids[pending++] = velocity_dma_write(
            &dma,
            dst,
            DMA_TEST_SEND_OFFSET,
            DMA_TEST_RX_BASE + cid * sizeof(uint32_t),
            sizeof(uint32_t),
            0);

        dma_test_delay(DMA_TEST_INJECT_GAP);

        if (pending == DMA_TEST_MAX_INFLIGHT)
        {
            for (uint32_t i = 0; i < pending; i++)
            {
                velocity_dma_wait(&dma, ids[i]);
                if (velocity_dma_status(&dma, ids[i]) != VELOCITY_DMA_STATUS_DONE)
                {
                    failed = 1;
                }
            }
            pending = 0;
        }
    }

    for (uint32_t i = 0; i < pending; i++)
    {
        velocity_dma_wait(&dma, ids[i]);
        if (velocity_dma_status(&dma, ids[i]) != VELOCITY_DMA_STATUS_DONE)
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

    flex_barrier_all();

    if (cid == 0)
    {
        uint32_t transfers = ARCH_NUM_CLUSTER * (ARCH_NUM_CLUSTER - 1u);
        uint32_t latency_ns = (*velocity_ctrl_reg(VELOCITY_CTRL_REG_TIMER_LO)) / 1000u;

        if (failed)
        {
            flex_print("INJECTION_RATE_RESULT FAIL ");
        }
        else
        {
            flex_print("INJECTION_RATE_RESULT PASS ");
        }
        flex_print("clusters=");
        flex_print_int(ARCH_NUM_CLUSTER);
        flex_print(" gap=");
        flex_print_int(DMA_TEST_INJECT_GAP);
        flex_print(" transfers=");
        flex_print_int(transfers);
        flex_print(" latency_ns=");
        flex_print_int(latency_ns);
        flex_print(" error=");
        flex_print_int(velocity_dma_error(&dma));
        flex_print("\n");
        flex_eoc(failed);
    }

    return failed;
}
