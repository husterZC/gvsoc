#ifndef VELOCITY_PACKET_DECODE_HPP
#define VELOCITY_PACKET_DECODE_HPP

#include <cstdio>
#include <cstdint>
#include <cstring>

#include <vp/itf/io.hpp>

#include "collective_packet.hpp"

namespace velocity
{

constexpr uint32_t REMOTE_MAGIC = 0x56444d41; // "VDMA"

constexpr uint32_t REMOTE_PHASE_WRITE = 0;
constexpr uint32_t REMOTE_PHASE_READ_REQ = 1;
constexpr uint32_t REMOTE_PHASE_READ_RESP = 2;
constexpr uint32_t REMOTE_PHASE_LATENCY_PROBE = 3;
constexpr uint32_t REMOTE_PHASE_TWO_SEND = 4;
constexpr uint32_t REMOTE_PHASE_INNETWORK_COLLECTIVE = 5;

struct PacketHeader
{
    uint32_t magic;
    uint16_t dst_cluster;
    uint16_t src_cluster;
    uint32_t txn_id;
    uint32_t phase;
    uint32_t payload_size;
    uint32_t local_offset;
    uint32_t remote_offset;
    uint64_t network_enter_cycle;
    uint64_t network_exit_cycle;
};

struct PacketTraceInfo
{
    enum Kind
    {
        KIND_UNKNOWN,
        KIND_DMA,
        KIND_COLLECTIVE,
    };

    Kind kind = KIND_UNKNOWN;
    uint32_t packet_id = 0;
    uint32_t phase = 0;
    uint32_t src_cluster = 0;
    uint32_t dst_cluster = 0;
    uint32_t root_cluster = 0;
    uint32_t payload_size = 0;
    uint32_t bytes = 0;
    uint32_t seq = 0;
    uint32_t op = 0;
    uint32_t group_type = 0;
    uint32_t flags = 0;
    uint32_t local_offset = 0;
    uint32_t remote_offset = 0;
    uint64_t network_enter_cycle = 0;
    uint64_t network_exit_cycle = 0;
    uint64_t addr = 0;
    uint32_t req_size = 0;
    bool is_write = false;
    bool has_src = false;
    bool has_dst = false;
    bool has_root = false;
};

inline const char *packet_phase_name(uint32_t phase)
{
    switch (phase)
    {
        case REMOTE_PHASE_WRITE: return "write";
        case REMOTE_PHASE_READ_REQ: return "read_req";
        case REMOTE_PHASE_READ_RESP: return "read_resp";
        case REMOTE_PHASE_LATENCY_PROBE: return "latency_probe";
        case REMOTE_PHASE_TWO_SEND: return "two_send";
        case REMOTE_PHASE_INNETWORK_COLLECTIVE: return "innetwork_collective";
        default: return "unknown";
    }
}

inline const char *io_status_name(vp::IoReqStatus status)
{
    switch (status)
    {
        case vp::IO_REQ_OK: return "ok";
        case vp::IO_REQ_INVALID: return "invalid";
        case vp::IO_REQ_PENDING: return "pending";
        case vp::IO_REQ_DENIED: return "denied";
        default: return "unknown";
    }
}

inline const char *packet_trace_kind_name(PacketTraceInfo::Kind kind)
{
    switch (kind)
    {
        case PacketTraceInfo::KIND_DMA: return "dma";
        case PacketTraceInfo::KIND_COLLECTIVE: return "collective";
        default: return "unknown";
    }
}

inline void packet_trace_format_node(bool valid, uint32_t node, char *buffer, size_t size)
{
    if (valid)
    {
        snprintf(buffer, size, "%u", node);
    }
    else
    {
        snprintf(buffer, size, "na");
    }
}

inline const char *packet_trace_action_name(const PacketTraceInfo &info)
{
    if (info.kind == PacketTraceInfo::KIND_COLLECTIVE)
    {
        return innetwork_op_name(info.op);
    }
    if (info.kind == PacketTraceInfo::KIND_DMA)
    {
        return packet_phase_name(info.phase);
    }
    return "unknown";
}

inline void packet_trace_format_id(const PacketTraceInfo &info, char *buffer, size_t size)
{
    if (info.kind == PacketTraceInfo::KIND_COLLECTIVE)
    {
        snprintf(buffer, size, "coll:%u:%u:%s:%u",
            info.root_cluster, info.seq, innetwork_op_name(info.op), info.src_cluster);
    }
    else if (info.kind == PacketTraceInfo::KIND_DMA)
    {
        snprintf(buffer, size, "dma:%u:%u", info.src_cluster, info.packet_id);
    }
    else
    {
        snprintf(buffer, size, "unknown:0x%llx:%u",
            (unsigned long long)info.addr, info.req_size);
    }
}

inline bool packet_trace_decode(vp::IoReq *req, PacketTraceInfo &info)
{
    info = PacketTraceInfo();
    if (req != nullptr)
    {
        info.addr = req->get_addr();
        info.req_size = req->get_size();
        info.is_write = req->get_is_write();
    }

    if (req == nullptr || req->get_data() == nullptr || req->get_size() < sizeof(uint32_t))
    {
        return false;
    }

    uint32_t magic = 0;
    memcpy(&magic, req->get_data(), sizeof(magic));
    if (magic == INNETWORK_MAGIC && req->get_size() >= sizeof(InNetworkHeader))
    {
        InNetworkHeader header;
        memcpy(&header, req->get_data(), sizeof(header));

        info.kind = PacketTraceInfo::KIND_COLLECTIVE;
        info.packet_id = header.seq;
        info.phase = REMOTE_PHASE_INNETWORK_COLLECTIVE;
        info.src_cluster = header.src_cluster;
        info.root_cluster = header.root_cluster;
        info.payload_size = req->get_size() - sizeof(InNetworkHeader);
        info.bytes = header.bytes;
        info.seq = header.seq;
        info.op = header.op;
        info.group_type = header.group_type;
        info.flags = header.flags;
        info.local_offset = header.offset;
        info.remote_offset = header.offset;
        info.has_src = true;
        info.has_root = true;
        return true;
    }

    if (magic != REMOTE_MAGIC || req->get_size() < sizeof(PacketHeader))
    {
        return false;
    }

    PacketHeader header;
    memcpy(&header, req->get_data(), sizeof(PacketHeader));

    info.kind = PacketTraceInfo::KIND_DMA;
    info.packet_id = header.txn_id;
    info.phase = header.phase;
    info.src_cluster = header.src_cluster;
    info.dst_cluster = header.dst_cluster;
    info.payload_size = header.payload_size;
    info.local_offset = header.local_offset;
    info.remote_offset = header.remote_offset;
    info.network_enter_cycle = header.network_enter_cycle;
    info.network_exit_cycle = header.network_exit_cycle;
    info.has_src = true;
    info.has_dst = true;

    return true;
}

} // namespace velocity

#endif
