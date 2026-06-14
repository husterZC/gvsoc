#ifndef VELOCITY_COLLECTIVE_PACKET_HPP
#define VELOCITY_COLLECTIVE_PACKET_HPP

#include <algorithm>
#include <cstdint>

namespace velocity
{

constexpr uint32_t INNETWORK_MAGIC = 0x4c4f4356; // "VCOL"

constexpr uint8_t INNETWORK_OP_BROADCAST = 1;
constexpr uint8_t INNETWORK_OP_REDUCE_INT8_SUM = 2;
constexpr uint8_t INNETWORK_OP_SCATTER = 3;
constexpr uint8_t INNETWORK_OP_GATHER = 4;
constexpr uint8_t INNETWORK_OP_ALLTOALL = 5;

constexpr uint8_t INNETWORK_GROUP_ALL = 0;
constexpr uint8_t INNETWORK_GROUP_CONTIGUOUS_RANGE = 1;
constexpr uint8_t INNETWORK_GROUP_POWER2_ALIGNED_RANGE = 2;
constexpr uint8_t INNETWORK_GROUP_STRIDE = 3;
constexpr uint8_t INNETWORK_GROUP_NESTED_STRIDE = 4;

constexpr uint8_t INNETWORK_FLAG_DIRECT = 1 << 0;
constexpr uint8_t INNETWORK_FLAG_FINAL = 1 << 1;

struct InNetworkHeader
{
    uint32_t magic;
    uint32_t offset;
    uint32_t bytes;
    uint16_t seq;
    uint16_t root_cluster;
    uint16_t src_cluster;
    uint8_t op;
    uint8_t group_type;
    uint8_t dtype;
    uint8_t flags;
    // Compact group descriptor:
    //   CONTIGUOUS_RANGE:     base, count
    //   POWER2_ALIGNED_RANGE: base, log2_size in group_count
    //   STRIDE:               base, count, stride
    //   NESTED_STRIDE:        base, inner_count, inner_stride, outer_count, outer_stride
    uint16_t group_base;
    uint16_t group_count;
    uint16_t group_stride;
    uint16_t group_outer_count;
    uint16_t group_outer_stride;
};

static_assert(sizeof(InNetworkHeader) == 32, "In-network collective header must stay compact");

inline bool innetwork_group_contains(const InNetworkHeader &header, uint32_t cluster, uint32_t num_cluster)
{
    switch (header.group_type)
    {
        case INNETWORK_GROUP_ALL:
            return cluster < num_cluster;
        case INNETWORK_GROUP_CONTIGUOUS_RANGE:
            return cluster >= header.group_base && cluster < (uint32_t)header.group_base + header.group_count &&
                cluster < num_cluster;
        case INNETWORK_GROUP_POWER2_ALIGNED_RANGE:
        {
            uint32_t count = 1u << header.group_count;
            return cluster >= header.group_base && cluster < (uint32_t)header.group_base + count &&
                cluster < num_cluster;
        }
        case INNETWORK_GROUP_STRIDE:
            if (header.group_stride == 0 || cluster < header.group_base)
            {
                return false;
            }
            return ((cluster - header.group_base) % header.group_stride) == 0 &&
                ((cluster - header.group_base) / header.group_stride) < header.group_count &&
                cluster < num_cluster;
        case INNETWORK_GROUP_NESTED_STRIDE:
            if (header.group_stride == 0 || header.group_outer_stride == 0 || cluster < header.group_base)
            {
                return false;
            }
            for (uint32_t outer = 0; outer < header.group_outer_count; outer++)
            {
                uint32_t base = header.group_base + outer * header.group_outer_stride;
                if (cluster >= base && ((cluster - base) % header.group_stride) == 0 &&
                    ((cluster - base) / header.group_stride) < header.group_count &&
                    cluster < num_cluster)
                {
                    return true;
                }
            }
            return false;
        default:
            return false;
    }
}

inline uint32_t innetwork_group_count(const InNetworkHeader &header, uint32_t num_cluster)
{
    switch (header.group_type)
    {
        case INNETWORK_GROUP_ALL:
            return num_cluster;
        case INNETWORK_GROUP_CONTIGUOUS_RANGE:
            return header.group_base >= num_cluster ? 0 : std::min<uint32_t>(header.group_count, num_cluster - header.group_base);
        case INNETWORK_GROUP_POWER2_ALIGNED_RANGE:
            return header.group_base >= num_cluster ? 0 : std::min<uint32_t>(1u << header.group_count, num_cluster - header.group_base);
        case INNETWORK_GROUP_STRIDE:
        case INNETWORK_GROUP_NESTED_STRIDE:
        {
            uint32_t count = 0;
            for (uint32_t cluster = 0; cluster < num_cluster; cluster++)
            {
                if (innetwork_group_contains(header, cluster, num_cluster))
                {
                    count++;
                }
            }
            return count;
        }
        default:
            return 0;
    }
}

inline int innetwork_group_rank(const InNetworkHeader &header, uint32_t cluster, uint32_t num_cluster)
{
    int rank = 0;
    for (uint32_t current = 0; current < num_cluster; current++)
    {
        if (!innetwork_group_contains(header, current, num_cluster))
        {
            continue;
        }
        if (current == cluster)
        {
            return rank;
        }
        rank++;
    }
    return -1;
}

inline int innetwork_group_member(const InNetworkHeader &header, uint32_t rank, uint32_t num_cluster)
{
    uint32_t current_rank = 0;
    for (uint32_t cluster = 0; cluster < num_cluster; cluster++)
    {
        if (!innetwork_group_contains(header, cluster, num_cluster))
        {
            continue;
        }
        if (current_rank == rank)
        {
            return cluster;
        }
        current_rank++;
    }
    return -1;
}

inline const char *innetwork_op_name(uint32_t op)
{
    switch (op)
    {
        case INNETWORK_OP_BROADCAST: return "broadcast";
        case INNETWORK_OP_REDUCE_INT8_SUM: return "reduce_int8_sum";
        case INNETWORK_OP_SCATTER: return "scatter";
        case INNETWORK_OP_GATHER: return "gather";
        case INNETWORK_OP_ALLTOALL: return "alltoall";
        default: return "unknown";
    }
}

} // namespace velocity

#endif
