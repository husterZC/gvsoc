#include "velocity_runtime.h"
#include "max_bandwidth_test_config.h"

#define MAX_BW_STATUS_OFFSET 0x4000u
#define MAX_BW_FAIL_OFFSET   0x5000u
#define MAX_BW_BUFFER_BASE   0x10000u

#define MAX_BW_REPORT_FAILED   0u
#define MAX_BW_REPORT_OBSERVED 1u
#define MAX_BW_REPORT_EXPECTED 2u
#define MAX_BW_REPORT_INDEX    3u
#define MAX_BW_REPORT_WORDS    4u
#define MAX_BW_REPORT_PENDING  0xffffffffu
#define MAX_BW_REPORT_WAIT     1000000u

#if MAX_BANDWIDTH_TEST_TXN_SIZE == 0
#error "MAX_BANDWIDTH_TEST_TXN_SIZE must be greater than zero"
#endif

#if (MAX_BANDWIDTH_TEST_TXN_SIZE % 4) != 0
#error "MAX_BANDWIDTH_TEST_TXN_SIZE must be a multiple of 4 bytes"
#endif

#if MAX_BANDWIDTH_TEST_REPEAT == 0 || MAX_BANDWIDTH_TEST_REPEAT > 255
#error "MAX_BANDWIDTH_TEST_REPEAT must be from 1 to 255"
#endif

#define MAX_BW_WORDS_PER_SLOT (MAX_BANDWIDTH_TEST_TXN_SIZE / sizeof(uint32_t))

#if MAX_BANDWIDTH_TEST_MAX_INFLIGHT == 0
#error "MAX_BANDWIDTH_TEST_MAX_INFLIGHT must be greater than zero"
#endif

#if MAX_BANDWIDTH_TEST_SLOTS == 0
#error "MAX_BANDWIDTH_TEST_SLOTS must be greater than zero"
#endif

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

static inline uint32_t active_slots(void)
{
    return MAX_BANDWIDTH_TEST_REPEAT < MAX_BANDWIDTH_TEST_SLOTS ?
        MAX_BANDWIDTH_TEST_REPEAT : MAX_BANDWIDTH_TEST_SLOTS;
}

static inline uint32_t max_bw_pattern(uint32_t slot, uint32_t word)
{
    return 0xb7000000u | ((slot & 0x0000ffffu) << 8) | (word & 0x000000ffu);
}

static void fill_tx_buffer(volatile uint32_t *buffer, uint32_t slots)
{
    for (uint32_t slot = 0; slot < slots; slot++)
    {
        uint32_t base = slot * MAX_BW_WORDS_PER_SLOT;
        for (uint32_t word = 0; word < MAX_BW_WORDS_PER_SLOT; word++)
        {
            buffer[base + word] = max_bw_pattern(slot, word);
        }
    }
}

static void clear_rx_buffer(volatile uint32_t *buffer, uint32_t slots)
{
    for (uint32_t slot = 0; slot < slots; slot++)
    {
        uint32_t base = slot * MAX_BW_WORDS_PER_SLOT;
        for (uint32_t word = 0; word < MAX_BW_WORDS_PER_SLOT; word++)
        {
            buffer[base + word] = 0;
        }
    }
}

static uint32_t check_rx_buffer(
    volatile uint32_t *buffer,
    uint32_t slots,
    uint32_t *observed,
    uint32_t *expected,
    uint32_t *index)
{
    for (uint32_t slot = 0; slot < slots; slot++)
    {
        uint32_t base = slot * MAX_BW_WORDS_PER_SLOT;
        for (uint32_t word = 0; word < MAX_BW_WORDS_PER_SLOT; word++)
        {
            uint32_t got = buffer[base + word];
            uint32_t want = max_bw_pattern(slot, word);
            if (got != want)
            {
                *observed = got;
                *expected = want;
                *index = base + word;
                return 1;
            }
        }
    }

    return 0;
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

static uint32_t wait_report(volatile uint32_t *status)
{
    for (uint32_t i = 0; i < MAX_BW_REPORT_WAIT; i++)
    {
        if (status[MAX_BW_REPORT_FAILED] != MAX_BW_REPORT_PENDING)
        {
            return 0;
        }
        asm volatile("nop");
    }

    return 1;
}

static void print_result(
    uint32_t failed,
    uint32_t dst,
    uint32_t bytes,
    uint32_t elapsed_ns,
    uint32_t observed,
    uint32_t expected,
    uint32_t index)
{
    if (failed)
    {
        flex_print("MAX_BANDWIDTH_RESULT FAIL ");
    }
    else
    {
        flex_print("MAX_BANDWIDTH_RESULT PASS ");
    }

    flex_print("clusters=");
    flex_print_int(ARCH_NUM_CLUSTER);
    flex_print(" src=0 dst=");
    flex_print_int(dst);
    flex_print(" txn_size=");
    flex_print_int(MAX_BANDWIDTH_TEST_TXN_SIZE);
    flex_print(" repeat=");
    flex_print_int(MAX_BANDWIDTH_TEST_REPEAT);
    flex_print(" max_inflight=");
    flex_print_int(MAX_BANDWIDTH_TEST_MAX_INFLIGHT);
    flex_print(" slots=");
    flex_print_int(MAX_BANDWIDTH_TEST_SLOTS);
    flex_print(" bytes=");
    flex_print_int(bytes);
    flex_print(" elapsed_ns=");
    flex_print_int(elapsed_ns);
    flex_print(" mismatch_index=");
    flex_print_int(index);
    flex_print(" observed=");
    flex_print_int(observed);
    flex_print(" expected=");
    flex_print_int(expected);
    flex_print(" error=");
    flex_print_int(velocity_dma_error());
    flex_print("\n");
}

int main(void)
{
    uint32_t cid = flex_get_core_id();
    uint32_t dst = ARCH_NUM_CLUSTER - 1u;
    uint32_t slots = active_slots();
    uint32_t ids[MAX_BANDWIDTH_TEST_MAX_INFLIGHT];
    uint32_t pending = 0;
    uint32_t failed = 0;
    uint32_t observed = 0;
    uint32_t expected = 0;
    uint32_t mismatch_index = 0;
    uint64_t footprint = (uint64_t)MAX_BANDWIDTH_TEST_TXN_SIZE * slots;
    uint32_t elapsed_ns = 0;
    uint32_t bytes = MAX_BANDWIDTH_TEST_TXN_SIZE * MAX_BANDWIDTH_TEST_REPEAT;

    if (ARCH_NUM_CLUSTER < 2 ||
        (uint64_t)MAX_BW_BUFFER_BASE + footprint > ARCH_CLUSTER_TCDM_SIZE)
    {
        if (cid == 0)
        {
            print_result(1, dst, 0, 0, 0, 0, 0);
            flex_eoc(1);
        }
        return 1;
    }

    volatile uint32_t *buffer = (volatile uint32_t *)local(MAX_BW_BUFFER_BASE);

    if (cid == 0)
    {
        volatile uint32_t *status = (volatile uint32_t *)local(MAX_BW_STATUS_OFFSET);
        for (uint32_t i = 0; i < MAX_BW_REPORT_WORDS; i++)
        {
            status[i] = MAX_BW_REPORT_PENDING;
        }
        fill_tx_buffer(buffer, slots);
    }
    else if (cid == dst)
    {
        clear_rx_buffer(buffer, slots);
    }

    flex_barrier_all();

    if (cid == 0)
    {
        flex_timer_start();

        for (uint32_t transfer = 0; transfer < MAX_BANDWIDTH_TEST_REPEAT; transfer++)
        {
            uint32_t slot = transfer % slots;
            uint32_t offset = MAX_BW_BUFFER_BASE + slot * MAX_BANDWIDTH_TEST_TXN_SIZE;

            ids[pending++] = velocity_dma_write(
                dst,
                offset,
                offset,
                MAX_BANDWIDTH_TEST_TXN_SIZE,
                0);

            if (pending == MAX_BANDWIDTH_TEST_MAX_INFLIGHT)
            {
                wait_pending(ids, &pending, &failed);
            }
        }

        wait_pending(ids, &pending, &failed);
        elapsed_ns = div_u64_u32(flex_timer_elapsed_ps(), 1000u);
    }

    flex_barrier_all();

    if (cid == dst)
    {
        failed |= check_rx_buffer(buffer, slots, &observed, &expected, &mismatch_index);
        volatile uint32_t *report = (volatile uint32_t *)local(MAX_BW_FAIL_OFFSET);
        report[MAX_BW_REPORT_FAILED] = failed;
        report[MAX_BW_REPORT_OBSERVED] = observed;
        report[MAX_BW_REPORT_EXPECTED] = expected;
        report[MAX_BW_REPORT_INDEX] = mismatch_index;

        uint32_t id = velocity_dma_write(
            0,
            MAX_BW_FAIL_OFFSET,
            MAX_BW_STATUS_OFFSET,
            MAX_BW_REPORT_WORDS * sizeof(uint32_t),
            0);
        velocity_dma_wait(id);
    }

    flex_barrier_all();

    if (cid == 0)
    {
        volatile uint32_t *status = (volatile uint32_t *)local(MAX_BW_STATUS_OFFSET);
        failed |= wait_report(status);
        failed |= status[MAX_BW_REPORT_FAILED] != 0;
        observed = status[MAX_BW_REPORT_OBSERVED];
        expected = status[MAX_BW_REPORT_EXPECTED];
        mismatch_index = status[MAX_BW_REPORT_INDEX];
        print_result(failed, dst, bytes, elapsed_ns, observed, expected, mismatch_index);
        flex_eoc(failed);
    }

    return failed;
}
