#include "velocity_runtime.h"

#define TWO_SIDED_TX_OFFSET     0x0800u
#define TWO_SIDED_RX_OFFSET     0x1000u
#define TWO_SIDED_STATUS_OFFSET 0x1800u

static inline uint32_t two_sided_pattern(uint32_t src, uint32_t step)
{
    return 0x2d000000u | ((step & 0x000000ffu) << 16) | (src & 0x0000ffffu);
}

static uint32_t check_word(volatile uint32_t *word, uint32_t expected)
{
    return *word == expected ? 0u : 1u;
}

static void print_result(uint32_t failed, uint32_t peer_failed, uint32_t dst)
{
    if (failed || peer_failed)
    {
        flex_print("TWO_SIDED_RESULT FAIL ");
    }
    else
    {
        flex_print("TWO_SIDED_RESULT PASS ");
    }

    flex_print("clusters=");
    flex_print_int(ARCH_NUM_CLUSTER);
    flex_print(" src=0 dst=");
    flex_print_int(dst);
    flex_print(" local_failed=");
    flex_print_int(failed);
    flex_print(" peer_failed=");
    flex_print_int(peer_failed);
    flex_print(" error=");
    flex_print_int(velocity_dma_error());
    flex_print("\n");
}

int main(void)
{
    uint32_t cid = flex_get_core_id();
    uint32_t dst = ARCH_NUM_CLUSTER - 1u;
    uint32_t failed = 0;
    volatile uint32_t *tx = (volatile uint32_t *)local(TWO_SIDED_TX_OFFSET);
    volatile uint32_t *rx = (volatile uint32_t *)local(TWO_SIDED_RX_OFFSET);
    volatile uint32_t *status = (volatile uint32_t *)local(TWO_SIDED_STATUS_OFFSET);

    if (ARCH_NUM_CLUSTER < 2)
    {
        if (cid == 0)
        {
            print_result(1, 0, dst);
            flex_eoc(1);
        }
        return 1;
    }

    if (cid != 0 && cid != dst)
    {
        return 0;
    }

    *tx = 0;
    *rx = 0;
    *status = 0;

    if (cid == 0)
    {
        *tx = two_sided_pattern(0, 1);
        failed |= velocity_dma_send(dst, TWO_SIDED_TX_OFFSET, sizeof(uint32_t));
    }
    else
    {
        *rx = 0;
        failed |= velocity_dma_recv(0, TWO_SIDED_RX_OFFSET, sizeof(uint32_t));
        failed |= check_word(rx, two_sided_pattern(0, 1));
    }

    if (cid == dst)
    {
        *tx = two_sided_pattern(dst, 2);
        failed |= velocity_dma_send(0, TWO_SIDED_TX_OFFSET, sizeof(uint32_t));
    }
    else
    {
        *rx = 0;
        failed |= velocity_dma_recv(dst, TWO_SIDED_RX_OFFSET, sizeof(uint32_t));
        failed |= check_word(rx, two_sided_pattern(dst, 2));
    }

    if (cid == 0)
    {
        *tx = two_sided_pattern(0, 3);
        *rx = 0;
        failed |= velocity_dma_sendrecv(dst, TWO_SIDED_TX_OFFSET, TWO_SIDED_RX_OFFSET,
            sizeof(uint32_t));
        failed |= check_word(rx, two_sided_pattern(dst, 3));
    }
    else
    {
        *tx = two_sided_pattern(dst, 3);
        *rx = 0;
        failed |= velocity_dma_sendrecv(0, TWO_SIDED_TX_OFFSET, TWO_SIDED_RX_OFFSET,
            sizeof(uint32_t));
        failed |= check_word(rx, two_sided_pattern(0, 3));
    }

    if (cid == dst)
    {
        *status = failed;
        velocity_dma_send(0, TWO_SIDED_STATUS_OFFSET, sizeof(uint32_t));
    }
    else
    {
        uint32_t peer_failed;
        *status = 0xffffffffu;
        failed |= velocity_dma_recv(dst, TWO_SIDED_STATUS_OFFSET, sizeof(uint32_t));
        peer_failed = *status;
        print_result(failed, peer_failed, dst);
        flex_eoc(failed || peer_failed);
    }

    return failed;
}
