#ifndef VELOCITY_DMA_H
#define VELOCITY_DMA_H

#include <stdint.h>
#include "velocity_arch.h"

#define VELOCITY_DMA_REG_REMOTE_CLUSTER 0x00
#define VELOCITY_DMA_REG_LOCAL_OFFSET   0x04
#define VELOCITY_DMA_REG_REMOTE_OFFSET  0x08
#define VELOCITY_DMA_REG_SIZE           0x0c
#define VELOCITY_DMA_REG_TYPE           0x10
#define VELOCITY_DMA_REG_TXN_ID         0x14
#define VELOCITY_DMA_REG_START          0x18
#define VELOCITY_DMA_REG_STATUS         0x1c
#define VELOCITY_DMA_REG_QUERY_ID       0x20
#define VELOCITY_DMA_REG_DONE_ID        0x24
#define VELOCITY_DMA_REG_ERROR          0x28
#define VELOCITY_DMA_REG_CMD            0x2c
#define VELOCITY_DMA_REG_PROBE_ENTER_LO 0x30
#define VELOCITY_DMA_REG_PROBE_ENTER_HI 0x34
#define VELOCITY_DMA_REG_PROBE_EXIT_LO  0x38
#define VELOCITY_DMA_REG_PROBE_EXIT_HI  0x3c
#define VELOCITY_DMA_REG_PROBE_LAT_LO   0x40
#define VELOCITY_DMA_REG_PROBE_LAT_HI   0x44
#define VELOCITY_DMA_REG_PROBE_DST      0x48

#define VELOCITY_DMA_CMD_REMOTE_CLUSTER_MASK 0x0000ffffu
#define VELOCITY_DMA_CMD_TYPE_SHIFT          16
#define VELOCITY_DMA_CMD_TYPE_MASK           0x000000ffu
#define VELOCITY_DMA_CMD_TXN_ID_SHIFT        24
#define VELOCITY_DMA_CMD_TXN_ID_MASK         0x000000ffu

#define VELOCITY_DMA_TYPE_READ          0
#define VELOCITY_DMA_TYPE_WRITE         1
#define VELOCITY_DMA_TYPE_LATENCY_PROBE 2

#define VELOCITY_DMA_STATUS_IDLE        0
#define VELOCITY_DMA_STATUS_BUSY        1
#define VELOCITY_DMA_STATUS_DONE        2
#define VELOCITY_DMA_STATUS_ERROR       3

#define VELOCITY_DMA_ERROR_NONE         0

static inline volatile uint32_t *velocity_dma_reg(uint32_t offset)
{
    return (volatile uint32_t *)(ARCH_CLUSTER_REG_BASE + ARCH_DMA_REG_OFFSET + offset);
}

static inline uint32_t velocity_dma_cmd(
    uint32_t remote_cluster,
    uint32_t type,
    uint32_t txn_id)
{
    return (remote_cluster & VELOCITY_DMA_CMD_REMOTE_CLUSTER_MASK) |
        ((type & VELOCITY_DMA_CMD_TYPE_MASK) << VELOCITY_DMA_CMD_TYPE_SHIFT) |
        ((txn_id & VELOCITY_DMA_CMD_TXN_ID_MASK) << VELOCITY_DMA_CMD_TXN_ID_SHIFT);
}

static inline uint32_t velocity_dma_start(
    uint32_t remote_cluster,
    uint32_t local_offset,
    uint32_t remote_offset,
    uint32_t size,
    uint32_t type,
    uint32_t txn_id)
{
    *velocity_dma_reg(VELOCITY_DMA_REG_LOCAL_OFFSET) = local_offset;
    *velocity_dma_reg(VELOCITY_DMA_REG_REMOTE_OFFSET) = remote_offset;
    *velocity_dma_reg(VELOCITY_DMA_REG_SIZE) = size;
    *velocity_dma_reg(VELOCITY_DMA_REG_CMD) = velocity_dma_cmd(remote_cluster, type, txn_id);

    return *velocity_dma_reg(VELOCITY_DMA_REG_TXN_ID);
}

static inline uint32_t velocity_dma_write(
    uint32_t remote_cluster,
    uint32_t local_offset,
    uint32_t remote_offset,
    uint32_t size,
    uint32_t txn_id)
{
    return velocity_dma_start(remote_cluster, local_offset, remote_offset,
        size, VELOCITY_DMA_TYPE_WRITE, txn_id);
}

static inline uint32_t velocity_dma_read(
    uint32_t remote_cluster,
    uint32_t local_offset,
    uint32_t remote_offset,
    uint32_t size,
    uint32_t txn_id)
{
    return velocity_dma_start(remote_cluster, local_offset, remote_offset,
        size, VELOCITY_DMA_TYPE_READ, txn_id);
}

static inline uint32_t velocity_dma_probe(
    uint32_t remote_cluster,
    uint32_t txn_id)
{
    return velocity_dma_start(remote_cluster, 0, 0, 0,
        VELOCITY_DMA_TYPE_LATENCY_PROBE, txn_id);
}

static inline uint64_t velocity_dma_read64_regs(uint32_t lo_offset, uint32_t hi_offset)
{
    uint32_t hi;
    uint32_t lo;
    uint32_t hi_check;

    do
    {
        hi = *velocity_dma_reg(hi_offset);
        lo = *velocity_dma_reg(lo_offset);
        hi_check = *velocity_dma_reg(hi_offset);
    } while (hi != hi_check);

    return ((uint64_t)hi << 32) | lo;
}

static inline uint64_t velocity_dma_probe_enter_cycle(void)
{
    return velocity_dma_read64_regs(VELOCITY_DMA_REG_PROBE_ENTER_LO,
        VELOCITY_DMA_REG_PROBE_ENTER_HI);
}

static inline uint64_t velocity_dma_probe_exit_cycle(void)
{
    return velocity_dma_read64_regs(VELOCITY_DMA_REG_PROBE_EXIT_LO,
        VELOCITY_DMA_REG_PROBE_EXIT_HI);
}

static inline uint64_t velocity_dma_probe_latency_cycle(void)
{
    return velocity_dma_read64_regs(VELOCITY_DMA_REG_PROBE_LAT_LO,
        VELOCITY_DMA_REG_PROBE_LAT_HI);
}

static inline uint32_t velocity_dma_probe_dst(void)
{
    return *velocity_dma_reg(VELOCITY_DMA_REG_PROBE_DST);
}

static inline uint32_t velocity_dma_status(uint32_t txn_id)
{
    *velocity_dma_reg(VELOCITY_DMA_REG_QUERY_ID) = txn_id;
    return *velocity_dma_reg(VELOCITY_DMA_REG_STATUS);
}

static inline uint32_t velocity_dma_done_id(void)
{
    return *velocity_dma_reg(VELOCITY_DMA_REG_DONE_ID);
}

static inline uint32_t velocity_dma_error(void)
{
    return *velocity_dma_reg(VELOCITY_DMA_REG_ERROR);
}

static inline void velocity_dma_wait(uint32_t txn_id)
{
    while (velocity_dma_status(txn_id) == VELOCITY_DMA_STATUS_BUSY)
    {
        asm volatile("nop");
    }
}

#endif
