#include "velocity_runtime.h"

#define DMA_TEST_SRC_OFFSET  0x0800u
#define DMA_TEST_DST_OFFSET  0x1000u
#define DMA_TEST_READS       4u

static inline uint32_t dma_test_pattern(uint32_t src)
{
    return 0x7c000000u | (src & 0x0000ffffu);
}

int main(void)
{
    uint32_t cid = flex_get_core_id();
    volatile uint32_t *src_word = (volatile uint32_t *)local(DMA_TEST_SRC_OFFSET);
    volatile uint32_t *dst_words = (volatile uint32_t *)local(DMA_TEST_DST_OFFSET);
    uint32_t ids[DMA_TEST_READS];
    uint32_t failed = 0;

    *src_word = dma_test_pattern(cid);
    for (uint32_t i = 0; i < DMA_TEST_READS; i++)
    {
        dst_words[i] = 0;
    }

    flex_barrier_all();

    for (uint32_t i = 0; i < DMA_TEST_READS; i++)
    {
        uint32_t src = (cid + i + 1u) % ARCH_NUM_CLUSTER;
        ids[i] = velocity_dma_read(
            src,
            DMA_TEST_DST_OFFSET + i * sizeof(uint32_t),
            DMA_TEST_SRC_OFFSET,
            sizeof(uint32_t),
            0);
    }

    for (uint32_t i = 0; i < DMA_TEST_READS; i++)
    {
        uint32_t src = (cid + i + 1u) % ARCH_NUM_CLUSTER;
        velocity_dma_wait(ids[i]);
        if (velocity_dma_status(ids[i]) != VELOCITY_DMA_STATUS_DONE ||
            dst_words[i] != dma_test_pattern(src))
        {
            failed = 1;
        }
    }

    flex_barrier_all();

    if (failed)
    {
        flex_print("DMA_READ_FAIL cluster=");
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
            flex_print("DMA_READ_RESULT FAIL clusters=");
        }
        else
        {
            flex_print("DMA_READ_RESULT PASS clusters=");
        }
        flex_print_int(ARCH_NUM_CLUSTER);
        flex_print("\n");
        flex_eoc(failed);
    }

    return failed;
}
