#ifndef VELOCITY_DMA_COMMON_H
#define VELOCITY_DMA_COMMON_H

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
#define VELOCITY_DMA_REG_TWO_SEND       0x4c
#define VELOCITY_DMA_REG_TWO_RECV       0x50
#define VELOCITY_DMA_REG_TWO_SENDRECV   0x54
#define VELOCITY_DMA_REG_COLL_OP        0x58
#define VELOCITY_DMA_REG_COLL_GROUP     0x5c
#define VELOCITY_DMA_REG_COLL_ROOT      0x60
#define VELOCITY_DMA_REG_COLL_SEQ       0x64
#define VELOCITY_DMA_REG_COLL_GROUP_BASE         0x68
#define VELOCITY_DMA_REG_COLL_GROUP_COUNT        0x6c
#define VELOCITY_DMA_REG_COLL_GROUP_STRIDE       0x70
#define VELOCITY_DMA_REG_COLL_GROUP_OUTER_COUNT  0x74
#define VELOCITY_DMA_REG_COLL_GROUP_OUTER_STRIDE 0x78
#define VELOCITY_DMA_REG_COLL_SEND      0x7c
#define VELOCITY_DMA_REG_COLL_RECV      0x80
#define VELOCITY_DMA_REG_COLL_SENDRECV  0x84

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

#define VELOCITY_INNETWORK_OP_BROADCAST       1
#define VELOCITY_INNETWORK_OP_REDUCE_INT8_SUM 2
#define VELOCITY_INNETWORK_OP_SCATTER         3
#define VELOCITY_INNETWORK_OP_GATHER          4
#define VELOCITY_INNETWORK_OP_ALLTOALL        5

#define VELOCITY_INNETWORK_GROUP_ALL                  0
#define VELOCITY_INNETWORK_GROUP_CONTIGUOUS_RANGE     1
#define VELOCITY_INNETWORK_GROUP_POWER2_ALIGNED_RANGE 2
#define VELOCITY_INNETWORK_GROUP_STRIDE               3
#define VELOCITY_INNETWORK_GROUP_NESTED_STRIDE        4

typedef struct {
    uint32_t type;
    /*
     * Compact group descriptor:
     *   CONTIGUOUS_RANGE:     base, count
     *   POWER2_ALIGNED_RANGE: base, log2_size in count
     *   STRIDE:               base, count, stride
     *   NESTED_STRIDE:        base, inner_count in count, inner_stride in stride,
     *                         outer_count, outer_stride
     */
    uint32_t base;
    uint32_t count;
    uint32_t stride;
    uint32_t outer_count;
    uint32_t outer_stride;
} velocity_innetwork_group_t;

#define VELOCITY_DMA_DECLARE_API(prefix, reg_offset) \
static inline volatile uint32_t *prefix##_reg(uint32_t offset) \
{ \
    return (volatile uint32_t *)(uintptr_t)(ARCH_CLUSTER_REG_BASE + (reg_offset) + offset); \
} \
\
static inline uint32_t prefix##_cmd( \
    uint32_t remote_cluster, \
    uint32_t type, \
    uint32_t txn_id) \
{ \
    return (remote_cluster & VELOCITY_DMA_CMD_REMOTE_CLUSTER_MASK) | \
        ((type & VELOCITY_DMA_CMD_TYPE_MASK) << VELOCITY_DMA_CMD_TYPE_SHIFT) | \
        ((txn_id & VELOCITY_DMA_CMD_TXN_ID_MASK) << VELOCITY_DMA_CMD_TXN_ID_SHIFT); \
} \
\
static inline uint32_t prefix##_start( \
    uint32_t remote_cluster, \
    uint32_t local_offset, \
    uint32_t remote_offset, \
    uint32_t size, \
    uint32_t type, \
    uint32_t txn_id) \
{ \
    *prefix##_reg(VELOCITY_DMA_REG_LOCAL_OFFSET) = local_offset; \
    *prefix##_reg(VELOCITY_DMA_REG_REMOTE_OFFSET) = remote_offset; \
    *prefix##_reg(VELOCITY_DMA_REG_SIZE) = size; \
    *prefix##_reg(VELOCITY_DMA_REG_CMD) = prefix##_cmd(remote_cluster, type, txn_id); \
\
    return *prefix##_reg(VELOCITY_DMA_REG_TXN_ID); \
} \
\
static inline uint32_t prefix##_write( \
    uint32_t remote_cluster, \
    uint32_t local_offset, \
    uint32_t remote_offset, \
    uint32_t size, \
    uint32_t txn_id) \
{ \
    return prefix##_start(remote_cluster, local_offset, remote_offset, \
        size, VELOCITY_DMA_TYPE_WRITE, txn_id); \
} \
\
static inline uint32_t prefix##_read( \
    uint32_t remote_cluster, \
    uint32_t local_offset, \
    uint32_t remote_offset, \
    uint32_t size, \
    uint32_t txn_id) \
{ \
    return prefix##_start(remote_cluster, local_offset, remote_offset, \
        size, VELOCITY_DMA_TYPE_READ, txn_id); \
} \
\
static inline uint32_t prefix##_probe( \
    uint32_t remote_cluster, \
    uint32_t txn_id) \
{ \
    return prefix##_start(remote_cluster, 0, 0, 0, \
        VELOCITY_DMA_TYPE_LATENCY_PROBE, txn_id); \
} \
\
static inline uint32_t prefix##_send( \
    uint32_t remote_cluster, \
    uint32_t local_offset, \
    uint32_t size) \
{ \
    *prefix##_reg(VELOCITY_DMA_REG_REMOTE_CLUSTER) = remote_cluster; \
    *prefix##_reg(VELOCITY_DMA_REG_LOCAL_OFFSET) = local_offset; \
    *prefix##_reg(VELOCITY_DMA_REG_SIZE) = size; \
    *prefix##_reg(VELOCITY_DMA_REG_TWO_SEND) = 1; \
\
    return *prefix##_reg(VELOCITY_DMA_REG_ERROR); \
} \
\
static inline uint32_t prefix##_recv( \
    uint32_t remote_cluster, \
    uint32_t local_offset, \
    uint32_t size) \
{ \
    *prefix##_reg(VELOCITY_DMA_REG_REMOTE_CLUSTER) = remote_cluster; \
    *prefix##_reg(VELOCITY_DMA_REG_LOCAL_OFFSET) = local_offset; \
    *prefix##_reg(VELOCITY_DMA_REG_SIZE) = size; \
    *prefix##_reg(VELOCITY_DMA_REG_TWO_RECV) = 1; \
\
    return *prefix##_reg(VELOCITY_DMA_REG_ERROR); \
} \
\
static inline uint32_t prefix##_sendrecv( \
    uint32_t remote_cluster, \
    uint32_t send_offset, \
    uint32_t recv_offset, \
    uint32_t size) \
{ \
    *prefix##_reg(VELOCITY_DMA_REG_REMOTE_CLUSTER) = remote_cluster; \
    *prefix##_reg(VELOCITY_DMA_REG_LOCAL_OFFSET) = send_offset; \
    *prefix##_reg(VELOCITY_DMA_REG_REMOTE_OFFSET) = recv_offset; \
    *prefix##_reg(VELOCITY_DMA_REG_SIZE) = size; \
    *prefix##_reg(VELOCITY_DMA_REG_TWO_SENDRECV) = 1; \
\
    return *prefix##_reg(VELOCITY_DMA_REG_ERROR); \
} \
\
static inline void prefix##_innetwork_config( \
    uint32_t op, \
    const velocity_innetwork_group_t *group, \
    uint32_t root, \
    uint32_t seq, \
    uint32_t send_offset, \
    uint32_t recv_offset, \
    uint32_t bytes) \
{ \
    *prefix##_reg(VELOCITY_DMA_REG_COLL_OP) = op; \
    *prefix##_reg(VELOCITY_DMA_REG_COLL_GROUP) = group->type; \
    *prefix##_reg(VELOCITY_DMA_REG_COLL_ROOT) = root; \
    *prefix##_reg(VELOCITY_DMA_REG_COLL_SEQ) = seq; \
    *prefix##_reg(VELOCITY_DMA_REG_COLL_GROUP_BASE) = group->base; \
    *prefix##_reg(VELOCITY_DMA_REG_COLL_GROUP_COUNT) = group->count; \
    *prefix##_reg(VELOCITY_DMA_REG_COLL_GROUP_STRIDE) = group->stride; \
    *prefix##_reg(VELOCITY_DMA_REG_COLL_GROUP_OUTER_COUNT) = group->outer_count; \
    *prefix##_reg(VELOCITY_DMA_REG_COLL_GROUP_OUTER_STRIDE) = group->outer_stride; \
    *prefix##_reg(VELOCITY_DMA_REG_LOCAL_OFFSET) = send_offset; \
    *prefix##_reg(VELOCITY_DMA_REG_REMOTE_OFFSET) = recv_offset; \
    *prefix##_reg(VELOCITY_DMA_REG_SIZE) = bytes; \
} \
\
static inline uint32_t prefix##_innetwork_send( \
    uint32_t op, \
    const velocity_innetwork_group_t *group, \
    uint32_t root, \
    uint32_t seq, \
    uint32_t send_offset, \
    uint32_t recv_offset, \
    uint32_t bytes) \
{ \
    prefix##_innetwork_config(op, group, root, seq, send_offset, recv_offset, bytes); \
    *prefix##_reg(VELOCITY_DMA_REG_COLL_SEND) = 1; \
    return *prefix##_reg(VELOCITY_DMA_REG_ERROR); \
} \
\
static inline uint32_t prefix##_innetwork_recv( \
    uint32_t op, \
    const velocity_innetwork_group_t *group, \
    uint32_t root, \
    uint32_t seq, \
    uint32_t recv_offset, \
    uint32_t bytes) \
{ \
    prefix##_innetwork_config(op, group, root, seq, 0, recv_offset, bytes); \
    *prefix##_reg(VELOCITY_DMA_REG_COLL_RECV) = 1; \
    return *prefix##_reg(VELOCITY_DMA_REG_ERROR); \
} \
\
static inline uint32_t prefix##_innetwork_sendrecv( \
    uint32_t op, \
    const velocity_innetwork_group_t *group, \
    uint32_t root, \
    uint32_t seq, \
    uint32_t send_offset, \
    uint32_t recv_offset, \
    uint32_t bytes) \
{ \
    prefix##_innetwork_config(op, group, root, seq, send_offset, recv_offset, bytes); \
    *prefix##_reg(VELOCITY_DMA_REG_COLL_SENDRECV) = 1; \
    return *prefix##_reg(VELOCITY_DMA_REG_ERROR); \
} \
\
static inline uint64_t prefix##_read64_regs(uint32_t lo_offset, uint32_t hi_offset) \
{ \
    uint32_t hi; \
    uint32_t lo; \
    uint32_t hi_check; \
\
    do \
    { \
        hi = *prefix##_reg(hi_offset); \
        lo = *prefix##_reg(lo_offset); \
        hi_check = *prefix##_reg(hi_offset); \
    } while (hi != hi_check); \
\
    return ((uint64_t)hi << 32) | lo; \
} \
\
static inline uint64_t prefix##_probe_enter_cycle(void) \
{ \
    return prefix##_read64_regs(VELOCITY_DMA_REG_PROBE_ENTER_LO, \
        VELOCITY_DMA_REG_PROBE_ENTER_HI); \
} \
\
static inline uint64_t prefix##_probe_exit_cycle(void) \
{ \
    return prefix##_read64_regs(VELOCITY_DMA_REG_PROBE_EXIT_LO, \
        VELOCITY_DMA_REG_PROBE_EXIT_HI); \
} \
\
static inline uint64_t prefix##_probe_latency_cycle(void) \
{ \
    return prefix##_read64_regs(VELOCITY_DMA_REG_PROBE_LAT_LO, \
        VELOCITY_DMA_REG_PROBE_LAT_HI); \
} \
\
static inline uint32_t prefix##_probe_dst(void) \
{ \
    return *prefix##_reg(VELOCITY_DMA_REG_PROBE_DST); \
} \
\
static inline uint32_t prefix##_status(uint32_t txn_id) \
{ \
    *prefix##_reg(VELOCITY_DMA_REG_QUERY_ID) = txn_id; \
    return *prefix##_reg(VELOCITY_DMA_REG_STATUS); \
} \
\
static inline uint32_t prefix##_done_id(void) \
{ \
    return *prefix##_reg(VELOCITY_DMA_REG_DONE_ID); \
} \
\
static inline uint32_t prefix##_error(void) \
{ \
    return *prefix##_reg(VELOCITY_DMA_REG_ERROR); \
} \
\
static inline void prefix##_wait(uint32_t txn_id) \
{ \
    while (prefix##_status(txn_id) == VELOCITY_DMA_STATUS_BUSY) \
    { \
        asm volatile("nop"); \
    } \
}

#endif
