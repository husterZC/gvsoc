#ifndef VELOCITY_PACKET_DECODE_HPP
#define VELOCITY_PACKET_DECODE_HPP

#include <cstdint>
#include <cstring>

#include <vp/itf/io.hpp>

namespace velocity
{

constexpr uint32_t REMOTE_MAGIC = 0x56444d41; // "VDMA"

constexpr uint32_t REMOTE_PHASE_WRITE = 0;
constexpr uint32_t REMOTE_PHASE_READ_REQ = 1;
constexpr uint32_t REMOTE_PHASE_READ_RESP = 2;
constexpr uint32_t REMOTE_PHASE_LATENCY_PROBE = 3;
constexpr uint32_t REMOTE_PHASE_TWO_SEND = 4;

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
    uint32_t packet_id = 0;
    uint32_t phase = 0;
    uint32_t src_cluster = 0;
    uint32_t dst_cluster = 0;
    uint32_t payload_size = 0;
    uint32_t local_offset = 0;
    uint32_t remote_offset = 0;
    uint64_t network_enter_cycle = 0;
    uint64_t network_exit_cycle = 0;
    uint64_t addr = 0;
    uint32_t req_size = 0;
    bool is_write = false;
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

inline bool packet_trace_decode(vp::IoReq *req, PacketTraceInfo &info)
{
    if (req == nullptr || req->get_data() == nullptr || req->get_size() < sizeof(PacketHeader))
    {
        return false;
    }

    PacketHeader header;
    memcpy(&header, req->get_data(), sizeof(PacketHeader));
    if (header.magic != REMOTE_MAGIC)
    {
        return false;
    }

    info.packet_id = header.txn_id;
    info.phase = header.phase;
    info.src_cluster = header.src_cluster;
    info.dst_cluster = header.dst_cluster;
    info.payload_size = header.payload_size;
    info.local_offset = header.local_offset;
    info.remote_offset = header.remote_offset;
    info.network_enter_cycle = header.network_enter_cycle;
    info.network_exit_cycle = header.network_exit_cycle;
    info.addr = req->get_addr();
    info.req_size = req->get_size();
    info.is_write = req->get_is_write();

    return true;
}

} // namespace velocity

#endif
