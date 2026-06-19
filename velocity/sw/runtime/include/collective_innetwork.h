#ifndef VELOCITY_COLLECTIVE_INNETWORK_H
#define VELOCITY_COLLECTIVE_INNETWORK_H

#include <stdint.h>
#include "velocity_dma.h"

static inline velocity_innetwork_group_t collective_innetwork_group_all(void)
{
    velocity_innetwork_group_t group = {VELOCITY_INNETWORK_GROUP_ALL, 0, 0, 0, 0, 0};
    return group;
}

static inline void collective_innetwork_group_init_all(velocity_innetwork_group_t *group)
{
    group->type = VELOCITY_INNETWORK_GROUP_ALL;
    group->base = 0;
    group->count = 0;
    group->stride = 0;
    group->outer_count = 0;
    group->outer_stride = 0;
}

static inline velocity_innetwork_group_t collective_innetwork_group_contiguous_range(
    uint32_t base,
    uint32_t count)
{
    velocity_innetwork_group_t group = {
        VELOCITY_INNETWORK_GROUP_CONTIGUOUS_RANGE, base, count, 0, 0, 0
    };
    return group;
}

static inline void collective_innetwork_group_init_contiguous_range(
    velocity_innetwork_group_t *group,
    uint32_t base,
    uint32_t count)
{
    group->type = VELOCITY_INNETWORK_GROUP_CONTIGUOUS_RANGE;
    group->base = base;
    group->count = count;
    group->stride = 0;
    group->outer_count = 0;
    group->outer_stride = 0;
}

static inline velocity_innetwork_group_t collective_innetwork_group_power2_aligned_range(
    uint32_t base,
    uint32_t log2_size)
{
    velocity_innetwork_group_t group = {
        VELOCITY_INNETWORK_GROUP_POWER2_ALIGNED_RANGE, base, log2_size, 0, 0, 0
    };
    return group;
}

static inline void collective_innetwork_group_init_power2_aligned_range(
    velocity_innetwork_group_t *group,
    uint32_t base,
    uint32_t log2_size)
{
    group->type = VELOCITY_INNETWORK_GROUP_POWER2_ALIGNED_RANGE;
    group->base = base;
    group->count = log2_size;
    group->stride = 0;
    group->outer_count = 0;
    group->outer_stride = 0;
}

static inline velocity_innetwork_group_t collective_innetwork_group_stride_nodes(
    uint32_t base,
    uint32_t count,
    uint32_t stride)
{
    velocity_innetwork_group_t group = {
        VELOCITY_INNETWORK_GROUP_STRIDE, base, count, stride, 0, 0
    };
    return group;
}

static inline void collective_innetwork_group_init_stride_nodes(
    velocity_innetwork_group_t *group,
    uint32_t base,
    uint32_t count,
    uint32_t stride)
{
    group->type = VELOCITY_INNETWORK_GROUP_STRIDE;
    group->base = base;
    group->count = count;
    group->stride = stride;
    group->outer_count = 0;
    group->outer_stride = 0;
}

static inline velocity_innetwork_group_t collective_innetwork_group_nested_stride_nodes(
    uint32_t base,
    uint32_t inner_count,
    uint32_t inner_stride,
    uint32_t outer_count,
    uint32_t outer_stride)
{
    velocity_innetwork_group_t group = {
        VELOCITY_INNETWORK_GROUP_NESTED_STRIDE,
        base,
        inner_count,
        inner_stride,
        outer_count,
        outer_stride
    };
    return group;
}

static inline void collective_innetwork_group_init_nested_stride_nodes(
    velocity_innetwork_group_t *group,
    uint32_t base,
    uint32_t inner_count,
    uint32_t inner_stride,
    uint32_t outer_count,
    uint32_t outer_stride)
{
    group->type = VELOCITY_INNETWORK_GROUP_NESTED_STRIDE;
    group->base = base;
    group->count = inner_count;
    group->stride = inner_stride;
    group->outer_count = outer_count;
    group->outer_stride = outer_stride;
}

static inline uint32_t collective_innetwork_group_contains(
    const velocity_dma_port_t *port,
    const velocity_innetwork_group_t *group,
    uint32_t node)
{
    switch (group->type)
    {
        case VELOCITY_INNETWORK_GROUP_ALL:
            return node < port->node_count;
        case VELOCITY_INNETWORK_GROUP_CONTIGUOUS_RANGE:
            return node >= group->base && node < group->base + group->count &&
                node < port->node_count;
        case VELOCITY_INNETWORK_GROUP_POWER2_ALIGNED_RANGE:
            return node >= group->base && node < group->base + (1u << group->count) &&
                node < port->node_count;
        case VELOCITY_INNETWORK_GROUP_STRIDE:
            return group->stride != 0 && node >= group->base &&
                ((node - group->base) % group->stride) == 0 &&
                ((node - group->base) / group->stride) < group->count &&
                node < port->node_count;
        case VELOCITY_INNETWORK_GROUP_NESTED_STRIDE:
            if (group->stride == 0 || group->outer_stride == 0 || node < group->base)
            {
                return 0;
            }
            for (uint32_t outer = 0; outer < group->outer_count; outer++)
            {
                uint32_t base = group->base + outer * group->outer_stride;
                if (node >= base && ((node - base) % group->stride) == 0 &&
                    ((node - base) / group->stride) < group->count &&
                    node < port->node_count)
                {
                    return 1;
                }
            }
            return 0;
        default:
            return 0;
    }
}

static inline uint32_t collective_innetwork_group_count(
    const velocity_dma_port_t *port,
    const velocity_innetwork_group_t *group)
{
    uint32_t count = 0;
    for (uint32_t node = 0; node < port->node_count; node++)
    {
        count += collective_innetwork_group_contains(port, group, node);
    }
    return count;
}

static inline int32_t collective_innetwork_group_rank(
    const velocity_dma_port_t *port,
    const velocity_innetwork_group_t *group,
    uint32_t node)
{
    int32_t rank = 0;
    for (uint32_t current = 0; current < port->node_count; current++)
    {
        if (!collective_innetwork_group_contains(port, group, current))
        {
            continue;
        }
        if (current == node)
        {
            return rank;
        }
        rank++;
    }
    return -1;
}

static inline int32_t collective_innetwork_group_member(
    const velocity_dma_port_t *port,
    const velocity_innetwork_group_t *group,
    uint32_t rank)
{
    uint32_t current_rank = 0;
    for (uint32_t node = 0; node < port->node_count; node++)
    {
        if (!collective_innetwork_group_contains(port, group, node))
        {
            continue;
        }
        if (current_rank == rank)
        {
            return node;
        }
        current_rank++;
    }
    return -1;
}

static inline uint32_t collective_innetwork_broadcast(
    const velocity_dma_port_t *port,
    const velocity_innetwork_group_t *group,
    uint32_t root,
    uint32_t offset,
    uint32_t bytes,
    uint32_t seq)
{
    if (port->self_node == root)
    {
        return velocity_dma_innetwork_sendrecv(
            port, VELOCITY_INNETWORK_OP_BROADCAST, group, root, seq, offset, offset, bytes);
    }

    return velocity_dma_innetwork_recv(
        port, VELOCITY_INNETWORK_OP_BROADCAST, group, root, seq, offset, bytes);
}

static inline uint32_t collective_innetwork_reduce_int8_sum(
    const velocity_dma_port_t *port,
    const velocity_innetwork_group_t *group,
    uint32_t root,
    uint32_t send_offset,
    uint32_t recv_offset,
    uint32_t bytes,
    uint32_t seq)
{
    if (port->self_node == root)
    {
        return velocity_dma_innetwork_sendrecv(
            port, VELOCITY_INNETWORK_OP_REDUCE_INT8_SUM, group, root, seq,
            send_offset, recv_offset, bytes);
    }

    return velocity_dma_innetwork_send(
        port, VELOCITY_INNETWORK_OP_REDUCE_INT8_SUM, group, root, seq,
        send_offset, recv_offset, bytes);
}

static inline uint32_t collective_innetwork_scatter(
    const velocity_dma_port_t *port,
    const velocity_innetwork_group_t *group,
    uint32_t root,
    uint32_t send_offset,
    uint32_t recv_offset,
    uint32_t bytes,
    uint32_t seq)
{
    if (port->self_node == root)
    {
        return velocity_dma_innetwork_sendrecv(
            port, VELOCITY_INNETWORK_OP_SCATTER, group, root, seq,
            send_offset, recv_offset, bytes);
    }

    return velocity_dma_innetwork_recv(
        port, VELOCITY_INNETWORK_OP_SCATTER, group, root, seq, recv_offset, bytes);
}

static inline uint32_t collective_innetwork_gather(
    const velocity_dma_port_t *port,
    const velocity_innetwork_group_t *group,
    uint32_t root,
    uint32_t send_offset,
    uint32_t recv_offset,
    uint32_t bytes,
    uint32_t seq)
{
    if (port->self_node == root)
    {
        return velocity_dma_innetwork_sendrecv(
            port, VELOCITY_INNETWORK_OP_GATHER, group, root, seq,
            send_offset, recv_offset, bytes);
    }

    return velocity_dma_innetwork_send(
        port, VELOCITY_INNETWORK_OP_GATHER, group, root, seq,
        send_offset, recv_offset, bytes);
}

static inline uint32_t collective_innetwork_alltoall(
    const velocity_dma_port_t *port,
    const velocity_innetwork_group_t *group,
    uint32_t send_offset,
    uint32_t recv_offset,
    uint32_t bytes,
    uint32_t seq)
{
    int32_t root = collective_innetwork_group_member(port, group, 0);
    if (root < 0)
    {
        return 1;
    }

    return velocity_dma_innetwork_sendrecv(
        port, VELOCITY_INNETWORK_OP_ALLTOALL, group, (uint32_t)root, seq,
        send_offset, recv_offset, bytes);
}

#endif
