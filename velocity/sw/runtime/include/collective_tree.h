#ifndef VELOCITY_COLLECTIVE_TREE_H
#define VELOCITY_COLLECTIVE_TREE_H

#include <stdint.h>
#include "velocity_runtime.h"
#include "vector_lib.h"

#define COLLECTIVE_TREE_OK             0u
#define COLLECTIVE_TREE_RANK_INVALID   0xffffffffu
#define COLLECTIVE_TREE_ERROR_GROUP    0x00010000u
#define COLLECTIVE_TREE_ERROR_ROOT     0x00020000u
#define COLLECTIVE_TREE_ERROR_SCRATCH  0x00030000u
#define COLLECTIVE_TREE_ERROR_DMA      0x00040000u

typedef struct
{
    velocity_dma_port_t port;
    const uint32_t *nodes;
    uint32_t size;
    uint32_t rank;
    uint32_t radix;
    uint32_t scratch_offset;
    uint32_t scratch_stride;
    uint32_t scratch_slots;
} collective_tree_group_t;

static inline uint32_t collective_tree_min(uint32_t a, uint32_t b)
{
    return a < b ? a : b;
}

static inline uint32_t collective_tree_required_scratch_bytes(uint32_t radix, uint32_t stride)
{
    return radix * stride;
}

static inline void collective_tree_group_init(
    collective_tree_group_t *group,
    const velocity_dma_port_t *port,
    const uint32_t *nodes,
    uint32_t size,
    uint32_t radix,
    uint32_t scratch_offset,
    uint32_t scratch_stride,
    uint32_t scratch_slots)
{
    group->port = *port;
    group->nodes = nodes;
    group->size = size;
    group->rank = COLLECTIVE_TREE_RANK_INVALID;
    group->radix = radix;
    group->scratch_offset = scratch_offset;
    group->scratch_stride = scratch_stride;
    group->scratch_slots = scratch_slots;

    for (uint32_t rank = 0; rank < size; rank++)
    {
        if (nodes[rank] == port->self_node)
        {
            group->rank = rank;
            break;
        }
    }
}

static inline uint32_t collective_tree_group_active(const collective_tree_group_t *group)
{
    return group->rank != COLLECTIVE_TREE_RANK_INVALID;
}

static inline uint32_t collective_tree_valid_group(const collective_tree_group_t *group)
{
    if (group->nodes == 0 || group->size == 0 || group->radix == 0)
    {
        return 0;
    }
    if (group->rank != COLLECTIVE_TREE_RANK_INVALID && group->rank >= group->size)
    {
        return 0;
    }

    return 1;
}

static inline uint32_t collective_tree_tree_rank(
    const collective_tree_group_t *group,
    uint32_t root_rank)
{
    if (group->rank >= root_rank)
    {
        return group->rank - root_rank;
    }

    return group->size - (root_rank - group->rank);
}

static inline uint32_t collective_tree_group_rank_for_tree_rank(
    const collective_tree_group_t *group,
    uint32_t root_rank,
    uint32_t tree_rank)
{
    uint32_t rank = root_rank + tree_rank;

    if (rank >= group->size)
    {
        rank -= group->size;
    }

    return rank;
}

static inline uint32_t collective_tree_node_for_tree_rank(
    const collective_tree_group_t *group,
    uint32_t root_rank,
    uint32_t tree_rank)
{
    return group->nodes[collective_tree_group_rank_for_tree_rank(group, root_rank, tree_rank)];
}

static inline uint32_t collective_tree_parent_tree_rank(uint32_t tree_rank, uint32_t radix)
{
    return (tree_rank - 1u) / radix;
}

static inline uint32_t collective_tree_child_count(
    const collective_tree_group_t *group,
    uint32_t tree_rank)
{
    uint32_t first_child = tree_rank * group->radix + 1u;

    if (first_child >= group->size)
    {
        return 0;
    }

    return collective_tree_min(group->radix, group->size - first_child);
}

static inline uint32_t collective_tree_is_ancestor(
    uint32_t ancestor,
    uint32_t node,
    uint32_t radix)
{
    while (node > ancestor)
    {
        node = collective_tree_parent_tree_rank(node, radix);
        if (node == ancestor)
        {
            return 1;
        }
    }

    return node == ancestor;
}

static inline uint32_t collective_tree_next_child_on_path(
    uint32_t ancestor,
    uint32_t node,
    uint32_t radix)
{
    uint32_t child = node;

    while (node > 0)
    {
        uint32_t parent = collective_tree_parent_tree_rank(node, radix);
        if (parent == ancestor)
        {
            return child;
        }
        child = parent;
        node = parent;
    }

    return COLLECTIVE_TREE_RANK_INVALID;
}

static inline uint32_t collective_tree_scratch_offset(
    const collective_tree_group_t *group,
    uint32_t slot)
{
    return group->scratch_offset + slot * group->scratch_stride;
}

static inline uint32_t collective_tree_scratch_ready(
    const collective_tree_group_t *group,
    uint32_t bytes,
    uint32_t slots)
{
    if (bytes == 0)
    {
        return 1;
    }

    return group->scratch_stride >= bytes && group->scratch_slots >= slots;
}

static inline void collective_tree_copy(uint32_t dst_offset, uint32_t src_offset, uint32_t bytes)
{
    volatile uint8_t *dst = (volatile uint8_t *)(uintptr_t)local(dst_offset);
    volatile uint8_t *src = (volatile uint8_t *)(uintptr_t)local(src_offset);

    for (uint32_t i = 0; i < bytes; i++)
    {
        dst[i] = src[i];
    }
}

static inline uint32_t collective_tree_send(
    const collective_tree_group_t *group,
    uint32_t node,
    uint32_t offset,
    uint32_t bytes)
{
    uint32_t err;

    if (bytes == 0)
    {
        return COLLECTIVE_TREE_OK;
    }

    err = velocity_dma_send(&group->port, node, offset, bytes);
    return err == 0 ? COLLECTIVE_TREE_OK : (COLLECTIVE_TREE_ERROR_DMA | err);
}

static inline uint32_t collective_tree_recv(
    const collective_tree_group_t *group,
    uint32_t node,
    uint32_t offset,
    uint32_t bytes)
{
    uint32_t err;

    if (bytes == 0)
    {
        return COLLECTIVE_TREE_OK;
    }

    err = velocity_dma_recv(&group->port, node, offset, bytes);
    return err == 0 ? COLLECTIVE_TREE_OK : (COLLECTIVE_TREE_ERROR_DMA | err);
}

static inline uint32_t collective_tree_sendrecv(
    const collective_tree_group_t *group,
    uint32_t node,
    uint32_t send_offset,
    uint32_t recv_offset,
    uint32_t bytes)
{
    uint32_t err;

    if (bytes == 0)
    {
        return COLLECTIVE_TREE_OK;
    }

    err = velocity_dma_sendrecv(&group->port, node, send_offset, recv_offset, bytes);
    return err == 0 ? COLLECTIVE_TREE_OK : (COLLECTIVE_TREE_ERROR_DMA | err);
}

static inline uint32_t collective_tree_round_robin_rank(
    uint32_t round,
    uint32_t position,
    uint32_t total)
{
    if (position == 0)
    {
        return 0;
    }

    return 1u + ((position - 1u + round) % (total - 1u));
}

static inline uint32_t collective_tree_broadcast(
    const collective_tree_group_t *group,
    uint32_t root_rank,
    uint32_t buffer_offset,
    uint32_t bytes)
{
    uint32_t tree_rank;
    uint32_t child_count;

    if (!collective_tree_valid_group(group))
    {
        return COLLECTIVE_TREE_ERROR_GROUP;
    }
    if (!collective_tree_group_active(group))
    {
        return COLLECTIVE_TREE_OK;
    }
    if (root_rank >= group->size)
    {
        return COLLECTIVE_TREE_ERROR_ROOT;
    }

    tree_rank = collective_tree_tree_rank(group, root_rank);
    if (tree_rank != 0)
    {
        uint32_t parent_tree = collective_tree_parent_tree_rank(tree_rank, group->radix);
        uint32_t parent = collective_tree_node_for_tree_rank(group, root_rank, parent_tree);
        uint32_t status = collective_tree_recv(group, parent, buffer_offset, bytes);
        if (status != COLLECTIVE_TREE_OK)
        {
            return status;
        }
    }

    child_count = collective_tree_child_count(group, tree_rank);
    for (uint32_t i = 0; i < child_count; i++)
    {
        uint32_t child_tree = tree_rank * group->radix + 1u + i;
        uint32_t child = collective_tree_node_for_tree_rank(group, root_rank, child_tree);
        uint32_t status = collective_tree_send(group, child, buffer_offset, bytes);
        if (status != COLLECTIVE_TREE_OK)
        {
            return status;
        }
    }

    return COLLECTIVE_TREE_OK;
}

static inline uint32_t collective_tree_reduce_int8_sum(
    const collective_tree_group_t *group,
    uint32_t root_rank,
    uint32_t buffer_offset,
    uint32_t bytes)
{
    uint32_t tree_rank;
    uint32_t child_count;

    if (!collective_tree_valid_group(group))
    {
        return COLLECTIVE_TREE_ERROR_GROUP;
    }
    if (!collective_tree_group_active(group))
    {
        return COLLECTIVE_TREE_OK;
    }
    if (root_rank >= group->size)
    {
        return COLLECTIVE_TREE_ERROR_ROOT;
    }

    tree_rank = collective_tree_tree_rank(group, root_rank);
    child_count = collective_tree_child_count(group, tree_rank);
    if (!collective_tree_scratch_ready(group, bytes, collective_tree_min(group->radix, child_count)))
    {
        return COLLECTIVE_TREE_ERROR_SCRATCH;
    }

    for (uint32_t i = 0; i < child_count; i++)
    {
        uint32_t child_tree = tree_rank * group->radix + 1u + i;
        uint32_t child = collective_tree_node_for_tree_rank(group, root_rank, child_tree);
        uint32_t scratch = collective_tree_scratch_offset(group, i);
        uint32_t status = collective_tree_recv(group, child, scratch, bytes);
        if (status != COLLECTIVE_TREE_OK)
        {
            return status;
        }
        vector_lib_int8_add((uint32_t)local(scratch), (uint32_t)local(buffer_offset),
            (uint32_t)local(buffer_offset), bytes);
    }

    if (tree_rank != 0)
    {
        uint32_t parent_tree = collective_tree_parent_tree_rank(tree_rank, group->radix);
        uint32_t parent = collective_tree_node_for_tree_rank(group, root_rank, parent_tree);
        return collective_tree_send(group, parent, buffer_offset, bytes);
    }

    return COLLECTIVE_TREE_OK;
}

static inline uint32_t collective_tree_scatter(
    const collective_tree_group_t *group,
    uint32_t root_rank,
    uint32_t send_offset,
    uint32_t recv_offset,
    uint32_t bytes)
{
    uint32_t tree_rank;
    uint32_t parent = 0;
    uint32_t scratch;

    if (!collective_tree_valid_group(group))
    {
        return COLLECTIVE_TREE_ERROR_GROUP;
    }
    if (!collective_tree_group_active(group))
    {
        return COLLECTIVE_TREE_OK;
    }
    if (root_rank >= group->size)
    {
        return COLLECTIVE_TREE_ERROR_ROOT;
    }
    if (!collective_tree_scratch_ready(group, bytes, 1))
    {
        return COLLECTIVE_TREE_ERROR_SCRATCH;
    }

    tree_rank = collective_tree_tree_rank(group, root_rank);
    scratch = collective_tree_scratch_offset(group, 0);
    if (tree_rank != 0)
    {
        uint32_t parent_tree = collective_tree_parent_tree_rank(tree_rank, group->radix);
        parent = collective_tree_node_for_tree_rank(group, root_rank, parent_tree);
    }

    for (uint32_t target_tree = 0; target_tree < group->size; target_tree++)
    {
        if (target_tree == tree_rank)
        {
            if (tree_rank == 0)
            {
                uint32_t target_rank = collective_tree_group_rank_for_tree_rank(
                    group, root_rank, target_tree);
                collective_tree_copy(recv_offset, send_offset + target_rank * bytes, bytes);
            }
            else
            {
                uint32_t status = collective_tree_recv(group, parent, recv_offset, bytes);
                if (status != COLLECTIVE_TREE_OK)
                {
                    return status;
                }
            }
        }
        else if (collective_tree_is_ancestor(tree_rank, target_tree, group->radix))
        {
            uint32_t child_tree = collective_tree_next_child_on_path(
                tree_rank, target_tree, group->radix);
            uint32_t child = collective_tree_node_for_tree_rank(group, root_rank, child_tree);
            uint32_t source_offset = scratch;
            uint32_t status;

            if (tree_rank == 0)
            {
                uint32_t target_rank = collective_tree_group_rank_for_tree_rank(
                    group, root_rank, target_tree);
                source_offset = send_offset + target_rank * bytes;
            }
            else
            {
                status = collective_tree_recv(group, parent, scratch, bytes);
                if (status != COLLECTIVE_TREE_OK)
                {
                    return status;
                }
            }

            status = collective_tree_send(group, child, source_offset, bytes);
            if (status != COLLECTIVE_TREE_OK)
            {
                return status;
            }
        }
    }

    return COLLECTIVE_TREE_OK;
}

static inline uint32_t collective_tree_gather(
    const collective_tree_group_t *group,
    uint32_t root_rank,
    uint32_t send_offset,
    uint32_t recv_offset,
    uint32_t bytes)
{
    uint32_t tree_rank;
    uint32_t parent = 0;
    uint32_t scratch;

    if (!collective_tree_valid_group(group))
    {
        return COLLECTIVE_TREE_ERROR_GROUP;
    }
    if (!collective_tree_group_active(group))
    {
        return COLLECTIVE_TREE_OK;
    }
    if (root_rank >= group->size)
    {
        return COLLECTIVE_TREE_ERROR_ROOT;
    }
    if (!collective_tree_scratch_ready(group, bytes, 1))
    {
        return COLLECTIVE_TREE_ERROR_SCRATCH;
    }

    tree_rank = collective_tree_tree_rank(group, root_rank);
    scratch = collective_tree_scratch_offset(group, 0);
    if (tree_rank != 0)
    {
        uint32_t parent_tree = collective_tree_parent_tree_rank(tree_rank, group->radix);
        parent = collective_tree_node_for_tree_rank(group, root_rank, parent_tree);
    }

    for (uint32_t source_tree = 0; source_tree < group->size; source_tree++)
    {
        uint32_t source_rank = collective_tree_group_rank_for_tree_rank(
            group, root_rank, source_tree);

        if (source_tree == tree_rank)
        {
            if (tree_rank == 0)
            {
                collective_tree_copy(recv_offset + source_rank * bytes, send_offset, bytes);
            }
            else
            {
                uint32_t status = collective_tree_send(group, parent, send_offset, bytes);
                if (status != COLLECTIVE_TREE_OK)
                {
                    return status;
                }
            }
        }
        else if (collective_tree_is_ancestor(tree_rank, source_tree, group->radix))
        {
            uint32_t child_tree = collective_tree_next_child_on_path(
                tree_rank, source_tree, group->radix);
            uint32_t child = collective_tree_node_for_tree_rank(group, root_rank, child_tree);
            uint32_t destination_offset = scratch;
            uint32_t status;

            if (tree_rank == 0)
            {
                destination_offset = recv_offset + source_rank * bytes;
            }

            status = collective_tree_recv(group, child, destination_offset, bytes);
            if (status != COLLECTIVE_TREE_OK)
            {
                return status;
            }

            if (tree_rank != 0)
            {
                status = collective_tree_send(group, parent, scratch, bytes);
                if (status != COLLECTIVE_TREE_OK)
                {
                    return status;
                }
            }
        }
    }

    return COLLECTIVE_TREE_OK;
}

static inline uint32_t collective_tree_all_to_all(
    const collective_tree_group_t *group,
    uint32_t send_offset,
    uint32_t recv_offset,
    uint32_t bytes)
{
    uint32_t total;

    if (!collective_tree_valid_group(group))
    {
        return COLLECTIVE_TREE_ERROR_GROUP;
    }
    if (!collective_tree_group_active(group))
    {
        return COLLECTIVE_TREE_OK;
    }

    collective_tree_copy(recv_offset + group->rank * bytes, send_offset + group->rank * bytes, bytes);

    total = (group->size & 1u) == 0 ? group->size : group->size + 1u;
    for (uint32_t round = 0; round < total - 1u; round++)
    {
        for (uint32_t position = 0; position < total / 2u; position++)
        {
            uint32_t left = collective_tree_round_robin_rank(round, position, total);
            uint32_t right = collective_tree_round_robin_rank(round, total - 1u - position, total);

            if (left >= group->size || right >= group->size)
            {
                continue;
            }

            if (group->rank == left)
            {
                uint32_t peer = group->nodes[right];
                uint32_t status = collective_tree_sendrecv(
                    group, peer, send_offset + right * bytes, recv_offset + right * bytes, bytes);
                if (status != COLLECTIVE_TREE_OK)
                {
                    return status;
                }
            }
            else if (group->rank == right)
            {
                uint32_t peer = group->nodes[left];
                uint32_t status = collective_tree_sendrecv(
                    group, peer, send_offset + left * bytes, recv_offset + left * bytes, bytes);
                if (status != COLLECTIVE_TREE_OK)
                {
                    return status;
                }
            }
        }
    }

    return COLLECTIVE_TREE_OK;
}

#endif
