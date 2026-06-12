#include "velocity_runtime.h"
#include "fat_tree_test_config.h"

#define FAT_TREE_RX_BASE       0x1000u
#define FAT_TREE_TX_BASE       0x0800u
#define FAT_TREE_STATUS_BASE   0x4000u
#define FAT_TREE_FAIL_OFFSET   0x5000u

static inline uint32_t fat_tree_pattern(uint32_t src, uint32_t word)
{
    return 0x71000000u | ((src & 0x0000ffffu) << 8) | (word & 0x000000ffu);
}

static uint32_t div_u64_u32(uint64_t value, uint32_t divisor)
{
    uint64_t quotient = 0;
    uint64_t remainder = 0;

    for (int bit = 63; bit >= 0; bit--)
    {
        remainder = (remainder << 1) | ((value >> bit) & 1u);
        if (remainder >= divisor)
        {
            remainder -= divisor;
            quotient |= 1ull << bit;
        }
    }

    return (uint32_t)quotient;
}

static void wait_pending(uint32_t *ids, uint32_t *pending, uint32_t *failed)
{
    for (uint32_t i = 0; i < *pending; i++)
    {
        velocity_dma_wait(ids[i]);
        if (velocity_dma_status(ids[i]) != VELOCITY_DMA_STATUS_DONE)
        {
            *failed = 1;
        }
    }

    *pending = 0;
}

int main(void)
{
    uint32_t cid = flex_get_core_id();
    volatile uint32_t *rx = (volatile uint32_t *)local(FAT_TREE_RX_BASE);
    volatile uint32_t *tx = (volatile uint32_t *)local(FAT_TREE_TX_BASE);
    volatile uint32_t *status = (volatile uint32_t *)local(FAT_TREE_STATUS_BASE);
    volatile uint32_t *fail_word = (volatile uint32_t *)local(FAT_TREE_FAIL_OFFSET);
    uint32_t ids[FAT_TREE_TEST_MAX_INFLIGHT];
    uint32_t pending = 0;
    uint32_t failed = 0;

    for (uint32_t src = 0; src < ARCH_NUM_CLUSTER; src++)
    {
        for (uint32_t word = 0; word < FAT_TREE_TEST_WORDS; word++)
        {
            rx[src * FAT_TREE_TEST_WORDS + word] = 0;
        }
    }

    for (uint32_t word = 0; word < FAT_TREE_TEST_WORDS; word++)
    {
        tx[word] = fat_tree_pattern(cid, word);
        rx[cid * FAT_TREE_TEST_WORDS + word] = tx[word];
    }

    if (cid == 0)
    {
        for (uint32_t i = 0; i < ARCH_NUM_CLUSTER; i++)
        {
            status[i] = 1;
        }
    }

    flex_barrier_all();

    if (cid == 0)
    {
        flex_timer_start();
    }

    for (uint32_t dst = 0; dst < ARCH_NUM_CLUSTER; dst++)
    {
        if (dst == cid)
        {
            continue;
        }

        ids[pending++] = velocity_dma_write(
            dst,
            FAT_TREE_TX_BASE,
            FAT_TREE_RX_BASE + cid * FAT_TREE_TEST_WORDS * sizeof(uint32_t),
            FAT_TREE_TEST_WORDS * sizeof(uint32_t),
            0);

        if (pending == FAT_TREE_TEST_MAX_INFLIGHT)
        {
            wait_pending(ids, &pending, &failed);
        }
    }

    wait_pending(ids, &pending, &failed);

    flex_barrier_all();

    for (uint32_t src = 0; src < ARCH_NUM_CLUSTER; src++)
    {
        for (uint32_t word = 0; word < FAT_TREE_TEST_WORDS; word++)
        {
            if (rx[src * FAT_TREE_TEST_WORDS + word] != fat_tree_pattern(src, word))
            {
                failed = 1;
            }
        }
    }

    *fail_word = failed;
    if (cid == 0)
    {
        status[0] = failed;
    }
    else
    {
        uint32_t id = velocity_dma_write(
            0,
            FAT_TREE_FAIL_OFFSET,
            FAT_TREE_STATUS_BASE + cid * sizeof(uint32_t),
            sizeof(uint32_t),
            0);
        velocity_dma_wait(id);
        if (velocity_dma_status(id) != VELOCITY_DMA_STATUS_DONE)
        {
            failed = 1;
        }
    }

    flex_barrier_all();

    if (cid == 0)
    {
        uint32_t total_failed = failed;
        uint32_t elapsed_ns = div_u64_u32(flex_timer_elapsed_ps(), 1000u);
        uint32_t transfers = ARCH_NUM_CLUSTER * (ARCH_NUM_CLUSTER - 1u);
        uint32_t bytes = transfers * FAT_TREE_TEST_WORDS * sizeof(uint32_t);

        for (uint32_t i = 0; i < ARCH_NUM_CLUSTER; i++)
        {
            if (status[i] != 0)
            {
                total_failed = 1;
            }
        }

        if (total_failed)
        {
            flex_print("FAT_TREE_ALL_TO_ALL_RESULT FAIL ");
        }
        else
        {
            flex_print("FAT_TREE_ALL_TO_ALL_RESULT PASS ");
        }

        flex_print("clusters=");
        flex_print_int(ARCH_NUM_CLUSTER);
        flex_print(" radix=");
        flex_print_int(ARCH_UNIFIED_INTERCO_RADIX);
        flex_print(" link_latency=");
        flex_print_int(ARCH_UNIFIED_INTERCO_LINK_LATENCY);
        flex_print(" link_width=");
        flex_print_int(ARCH_UNIFIED_INTERCO_LINK_WIDTH);
        flex_print(" words=");
        flex_print_int(FAT_TREE_TEST_WORDS);
        flex_print(" max_inflight=");
        flex_print_int(FAT_TREE_TEST_MAX_INFLIGHT);
        flex_print(" transfers=");
        flex_print_int(transfers);
        flex_print(" bytes=");
        flex_print_int(bytes);
        flex_print(" elapsed_ns=");
        flex_print_int(elapsed_ns);
        flex_print("\n");

        flex_eoc(total_failed);
    }

    return failed;
}
