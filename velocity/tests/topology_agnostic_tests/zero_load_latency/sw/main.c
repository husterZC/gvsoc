#include "velocity_runtime.h"

static uint32_t latency_failed(
    const velocity_dma_port_t *dma,
    uint32_t dst,
    uint64_t enter,
    uint64_t exit,
    uint64_t latency)
{
    if (velocity_dma_probe_dst(dma) != dst)
    {
        return 1;
    }
    if (exit < enter)
    {
        return 1;
    }
    if (latency != exit - enter)
    {
        return 1;
    }

    return 0;
}

int main(void)
{
    uint32_t cid = flex_get_core_id();

    flex_barrier_all();

    if (cid != 0)
    {
        return 0;
    }

    uint32_t failed = 0;
    velocity_dma_port_t dma = flex_dma_port();

    for (uint32_t dst = 1; dst < ARCH_NUM_CLUSTER; dst++)
    {
        uint32_t id = velocity_dma_probe(&dma, dst, 0);
        velocity_dma_wait(&dma, id);

        uint32_t status = velocity_dma_status(&dma, id);
        uint64_t enter = velocity_dma_probe_enter_cycle(&dma);
        uint64_t exit = velocity_dma_probe_exit_cycle(&dma);
        uint64_t latency = velocity_dma_probe_latency_cycle(&dma);
        uint32_t row_failed = status != VELOCITY_DMA_STATUS_DONE ||
            latency_failed(&dma, dst, enter, exit, latency);

        failed |= row_failed;

        if (row_failed)
        {
            flex_print("ZERO_LOAD_LATENCY_ROW FAIL ");
        }
        else
        {
            flex_print("ZERO_LOAD_LATENCY_ROW PASS ");
        }

        flex_print("src=0 dst=");
        flex_print_int(dst);
        flex_print(" enter_hi=");
        flex_print_int((uint32_t)(enter >> 32));
        flex_print(" enter_lo=");
        flex_print_int((uint32_t)enter);
        flex_print(" exit_hi=");
        flex_print_int((uint32_t)(exit >> 32));
        flex_print(" exit_lo=");
        flex_print_int((uint32_t)exit);
        flex_print(" latency_hi=");
        flex_print_int((uint32_t)(latency >> 32));
        flex_print(" latency_lo=");
        flex_print_int((uint32_t)latency);
        flex_print(" status=");
        flex_print_int(status);
        flex_print("\n");
    }

    if (failed)
    {
        flex_print("ZERO_LOAD_LATENCY_RESULT FAIL clusters=");
    }
    else
    {
        flex_print("ZERO_LOAD_LATENCY_RESULT PASS clusters=");
    }
    flex_print_int(ARCH_NUM_CLUSTER);
    flex_print("\n");

    flex_eoc(failed);
    return failed;
}
