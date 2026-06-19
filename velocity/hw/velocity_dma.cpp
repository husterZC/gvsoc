/*
 * Copyright (C) 2024 ETH Zurich and University of Bologna
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <deque>
#include <memory>
#include <unordered_map>
#include <vector>

#include <vp/vp.hpp>
#include <vp/itf/io.hpp>

#include "collective_packet.hpp"
#include "packet_trace.hpp"

namespace {

constexpr uint64_t REG_REMOTE_CLUSTER = 0x00;
constexpr uint64_t REG_LOCAL_OFFSET   = 0x04;
constexpr uint64_t REG_REMOTE_OFFSET  = 0x08;
constexpr uint64_t REG_SIZE           = 0x0c;
constexpr uint64_t REG_TYPE           = 0x10;
constexpr uint64_t REG_TXN_ID         = 0x14;
constexpr uint64_t REG_START          = 0x18;
constexpr uint64_t REG_STATUS         = 0x1c;
constexpr uint64_t REG_QUERY_ID       = 0x20;
constexpr uint64_t REG_DONE_ID        = 0x24;
constexpr uint64_t REG_ERROR          = 0x28;
constexpr uint64_t REG_CMD            = 0x2c;
constexpr uint64_t REG_PROBE_ENTER_LO = 0x30;
constexpr uint64_t REG_PROBE_ENTER_HI = 0x34;
constexpr uint64_t REG_PROBE_EXIT_LO  = 0x38;
constexpr uint64_t REG_PROBE_EXIT_HI  = 0x3c;
constexpr uint64_t REG_PROBE_LAT_LO   = 0x40;
constexpr uint64_t REG_PROBE_LAT_HI   = 0x44;
constexpr uint64_t REG_PROBE_DST      = 0x48;
constexpr uint64_t REG_TWO_SEND       = 0x4c;
constexpr uint64_t REG_TWO_RECV       = 0x50;
constexpr uint64_t REG_TWO_SENDRECV   = 0x54;
constexpr uint64_t REG_COLL_OP        = 0x58;
constexpr uint64_t REG_COLL_GROUP     = 0x5c;
constexpr uint64_t REG_COLL_ROOT      = 0x60;
constexpr uint64_t REG_COLL_SEQ       = 0x64;
constexpr uint64_t REG_COLL_GROUP_BASE         = 0x68;
constexpr uint64_t REG_COLL_GROUP_COUNT        = 0x6c;
constexpr uint64_t REG_COLL_GROUP_STRIDE       = 0x70;
constexpr uint64_t REG_COLL_GROUP_OUTER_COUNT  = 0x74;
constexpr uint64_t REG_COLL_GROUP_OUTER_STRIDE = 0x78;
constexpr uint64_t REG_COLL_SEND      = 0x7c;
constexpr uint64_t REG_COLL_RECV      = 0x80;
constexpr uint64_t REG_COLL_SENDRECV  = 0x84;

constexpr uint32_t CMD_REMOTE_CLUSTER_MASK = 0x0000ffff;
constexpr uint32_t CMD_TYPE_SHIFT = 16;
constexpr uint32_t CMD_TYPE_MASK = 0x000000ff;
constexpr uint32_t CMD_TXN_ID_SHIFT = 24;
constexpr uint32_t CMD_TXN_ID_MASK = 0x000000ff;

constexpr uint32_t DMA_TYPE_READ = 0;
constexpr uint32_t DMA_TYPE_WRITE = 1;
constexpr uint32_t DMA_TYPE_LATENCY_PROBE = 2;

constexpr uint32_t DMA_STATUS_IDLE = 0;
constexpr uint32_t DMA_STATUS_BUSY = 1;
constexpr uint32_t DMA_STATUS_DONE = 2;
constexpr uint32_t DMA_STATUS_ERROR = 3;

constexpr uint32_t DMA_ERROR_NONE = 0;
constexpr uint32_t DMA_ERROR_BAD_CLUSTER = 1;
constexpr uint32_t DMA_ERROR_BAD_TYPE = 2;
constexpr uint32_t DMA_ERROR_BAD_SIZE = 3;
constexpr uint32_t DMA_ERROR_BAD_LOCAL_OFFSET = 4;
constexpr uint32_t DMA_ERROR_BAD_REMOTE_OFFSET = 5;
constexpr uint32_t DMA_ERROR_NO_SLOT = 6;
constexpr uint32_t DMA_ERROR_DUPLICATE_ID = 7;
constexpr uint32_t DMA_ERROR_BUFFER_TOO_SMALL = 8;
constexpr uint32_t DMA_ERROR_LOCAL_ACCESS = 9;
constexpr uint32_t DMA_ERROR_REMOTE_ACCESS = 10;
constexpr uint32_t DMA_ERROR_BAD_DEST = 11;

constexpr uint32_t REMOTE_PHASE_WRITE = 0;
constexpr uint32_t REMOTE_PHASE_READ_REQ = 1;
constexpr uint32_t REMOTE_PHASE_READ_RESP = 2;
constexpr uint32_t REMOTE_PHASE_LATENCY_PROBE = 3;
constexpr uint32_t REMOTE_PHASE_TWO_SEND = 4;
constexpr uint32_t REMOTE_PHASE_INNETWORK_COLLECTIVE = 5;

struct RemoteHeader
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

constexpr uint32_t REMOTE_MAGIC = 0x56444d41; // "VDMA"

const char *dma_type_name(uint32_t type)
{
    switch (type)
    {
        case DMA_TYPE_READ: return "read";
        case DMA_TYPE_WRITE: return "write";
        case DMA_TYPE_LATENCY_PROBE: return "latency_probe";
        default: return "unknown";
    }
}

const char *dma_status_name(uint32_t status)
{
    switch (status)
    {
        case DMA_STATUS_IDLE: return "idle";
        case DMA_STATUS_BUSY: return "busy";
        case DMA_STATUS_DONE: return "done";
        case DMA_STATUS_ERROR: return "error";
        default: return "unknown";
    }
}

} // namespace

class VelocityDma : public vp::Component
{
public:
    VelocityDma(vp::ComponentConf &config);

private:
    struct Txn
    {
        uint32_t id;
        uint32_t type;
        uint32_t remote_cluster;
        uint32_t local_offset;
        uint32_t remote_offset;
        uint32_t size;
        uint32_t status;
        uint32_t error;
        std::vector<uint8_t> buffer;
        vp::ClockEvent *event;
    };

    struct BlockingOp;
    struct CollectiveOp;

    struct OutgoingPacket
    {
        Txn *txn;
        BlockingOp *blocking_op = NULL;
        CollectiveOp *collective_op = NULL;
        uint32_t phase;
        uint32_t dst_cluster;
        uint64_t completion_latency;
        std::vector<uint8_t> data;
        vp::IoReq req;
    };

    struct PendingRemoteWrite
    {
        Txn *txn;
        vp::IoReq *req;
        vp::ClockEvent *event;
    };

    struct BlockingOp
    {
        uint32_t packet_id;
        uint32_t peer_cluster;
        uint32_t send_offset;
        uint32_t recv_offset;
        uint32_t size;
        bool wait_send;
        bool wait_recv;
        bool send_done;
        bool recv_done;
        uint32_t error;
        vp::IoReq *regs_req;
        std::vector<uint8_t> send_buffer;
    };

    struct PendingTwoSidedSend
    {
        uint32_t src_cluster;
        uint32_t size;
        vp::IoReq *req;
    };

    struct PendingTwoSidedRecv
    {
        BlockingOp *op;
        vp::IoReq *remote_req;
        vp::ClockEvent *event;
    };

    struct CollectiveOp
    {
        uint32_t packet_id;
        velocity::InNetworkHeader header;
        uint32_t send_offset;
        uint32_t recv_offset;
        uint32_t send_size;
        uint32_t recv_size;
        uint32_t expected_recv;
        uint32_t received;
        bool wait_send;
        bool wait_recv;
        bool send_done;
        bool recv_done;
        uint32_t error;
        vp::IoReq *regs_req;
        std::vector<uint8_t> send_buffer;
    };

    struct PendingCollectivePacket
    {
        vp::IoReq *req;
        velocity::InNetworkHeader header;
    };

    static vp::IoReqStatus regs_req(vp::Block *__this, vp::IoReq *req);
    static vp::IoReqStatus remote_req(vp::Block *__this, vp::IoReq *req);
    static void remote_grant(vp::Block *__this, vp::IoReq *req);
    static void remote_resp(vp::Block *__this, vp::IoReq *req);
    static void send_packet_event(vp::Block *__this, vp::ClockEvent *event);
    static void remote_write_event(vp::Block *__this, vp::ClockEvent *event);
    static void two_sided_recv_event(vp::Block *__this, vp::ClockEvent *event);
    static void complete_event(vp::Block *__this, vp::ClockEvent *event);

    uint32_t allocate_txn_id();
    uint32_t get_status(uint32_t txn_id);
    bool validate_common(Txn *txn);
    bool local_access(Txn *txn, const char *context, uint32_t offset, uint32_t size, uint8_t *data,
        bool is_write, uint64_t &latency);
    std::unique_ptr<OutgoingPacket> build_packet(Txn *txn, uint32_t packet_id, uint32_t dst_cluster,
        uint32_t phase, uint32_t local_offset, uint32_t remote_offset, uint32_t payload_size,
        uint8_t *payload, uint64_t completion_latency);
    bool send_packet(std::unique_ptr<OutgoingPacket> packet);
    bool issue_packet(OutgoingPacket *packet);
    bool schedule_packet_send(std::unique_ptr<OutgoingPacket> packet, uint64_t cycles);
    void complete_packet(OutgoingPacket *packet, vp::IoReqStatus status, bool synchronous);
    void schedule_remote_write_response(Txn *txn, vp::IoReq *req, uint64_t cycles);
    bool submit_remote_write(Txn *txn, uint64_t latency);
    bool submit_remote_read_req(Txn *txn, uint64_t latency);
    bool submit_remote_read_resp(uint32_t dst_cluster, uint32_t txn_id, uint32_t local_offset,
        uint32_t remote_offset, uint32_t size, uint8_t *payload, uint64_t latency);
    bool submit_latency_probe(Txn *txn);
    void stamp_latency_probe_enter(OutgoingPacket *packet);
    void record_latency_probe(OutgoingPacket *packet);
    vp::IoReqStatus launch_two_sided(vp::IoReq *req, uint64_t command);
    bool validate_two_sided(BlockingOp *op);
    void post_two_sided_recv(BlockingOp *op);
    void unpost_two_sided_recv(BlockingOp *op);
    bool match_pending_two_sided_send(BlockingOp *op);
    bool consume_two_sided_send(BlockingOp *op, vp::IoReq *remote_req);
    bool start_two_sided_send(BlockingOp *op);
    bool handle_two_sided_send(vp::IoReq *req, const RemoteHeader &header);
    void schedule_two_sided_recv_completion(BlockingOp *op, vp::IoReq *remote_req, uint64_t cycles);
    void complete_two_sided_op(BlockingOp *op);
    vp::IoReqStatus launch_collective(vp::IoReq *req, uint64_t command);
    bool validate_collective(CollectiveOp *op);
    bool start_collective_send(CollectiveOp *op);
    bool handle_collective_packet(vp::IoReq *req, const velocity::InNetworkHeader &header);
    bool match_collective_packet(CollectiveOp *op, vp::IoReq *remote_req,
        const velocity::InNetworkHeader &header);
    bool collective_headers_match(CollectiveOp *op, const velocity::InNetworkHeader &header);
    void complete_collective_op(CollectiveOp *op);
    uint32_t collective_payload_size(const velocity::InNetworkHeader &header);
    void finish_txn(Txn *txn, uint32_t status, uint32_t error);
    void launch_txn(uint32_t requested_id);
    void launch_packed_cmd(uint32_t cmd);
    void schedule_completion(Txn *txn, uint64_t cycles);
    void remove_inflight(Txn *txn);
    bool read_u32(vp::IoReq *req, uint32_t value);
    bool write_u32(vp::IoReq *req, uint32_t &reg);
    void trace_flow(const char *event, const char *direction, vp::IoReq *req,
        vp::IoReqStatus status);

    vp::Trace trace;
    vp::Trace flow;
    vp::IoSlave regs_itf;
    vp::IoSlave remote_in_itf;
    vp::IoMaster tcdm_itf;
    vp::IoMaster remote_out_itf;

    uint32_t cluster_id;
    uint32_t num_cluster;
    uint32_t tcdm_size;
    uint32_t bus_width;
    uint32_t max_inflight;
    uint32_t read_buffer_size;
    uint32_t write_buffer_size;
    uint32_t base_latency;
    uint32_t cluster_stride;

    uint32_t remote_cluster_reg = 0;
    uint32_t local_offset_reg = 0;
    uint32_t remote_offset_reg = 0;
    uint32_t size_reg = 0;
    uint32_t type_reg = 0;
    uint32_t txn_id_reg = 0;
    uint32_t query_id_reg = 0;
    uint32_t done_id_reg = 0;
    uint32_t last_error = DMA_ERROR_NONE;
    uint32_t coll_op_reg = 0;
    uint32_t coll_group_reg = 0;
    uint32_t coll_root_reg = 0;
    uint32_t coll_seq_reg = 0;
    uint32_t coll_group_base_reg = 0;
    uint32_t coll_group_count_reg = 0;
    uint32_t coll_group_stride_reg = 0;
    uint32_t coll_group_outer_count_reg = 0;
    uint32_t coll_group_outer_stride_reg = 0;
    uint32_t next_txn_id = 1;
    uint32_t last_probe_dst = 0;
    uint64_t last_probe_enter_cycle = 0;
    uint64_t last_probe_exit_cycle = 0;
    uint64_t last_probe_latency = 0;

    std::deque<Txn *> ready;
    std::unordered_map<uint32_t, std::unique_ptr<Txn>> txns;
    std::unordered_map<vp::IoReq *, std::unique_ptr<OutgoingPacket>> remote_pending;
    std::unordered_map<OutgoingPacket *, std::unique_ptr<OutgoingPacket>> delayed_packets;
    std::unordered_map<PendingRemoteWrite *, std::unique_ptr<PendingRemoteWrite>> pending_remote_writes;
    std::unordered_map<BlockingOp *, std::unique_ptr<BlockingOp>> blocking_ops;
    std::unordered_map<PendingTwoSidedRecv *, std::unique_ptr<PendingTwoSidedRecv>> pending_two_sided_recvs;
    std::unordered_map<CollectiveOp *, std::unique_ptr<CollectiveOp>> collective_ops;
    std::deque<BlockingOp *> posted_two_sided_recvs;
    std::deque<PendingTwoSidedSend> pending_two_sided_sends;
    std::deque<PendingCollectivePacket> pending_collective_packets;
};

VelocityDma::VelocityDma(vp::ComponentConf &config)
    : vp::Component(config)
{
    this->traces.new_trace("trace", &this->trace, vp::DEBUG);
    this->traces.new_trace("flow", &this->flow, vp::DEBUG);

    this->cluster_id = this->get_js_config()->get_child_int("cluster_id");
    this->num_cluster = this->get_js_config()->get_child_int("num_cluster");
    this->tcdm_size = this->get_js_config()->get_child_int("tcdm_size");
    this->bus_width = this->get_js_config()->get_child_int("bus_width");
    this->max_inflight = this->get_js_config()->get_child_int("max_inflight");
    this->read_buffer_size = this->get_js_config()->get_child_int("read_buffer_size");
    this->write_buffer_size = this->get_js_config()->get_child_int("write_buffer_size");
    this->base_latency = this->get_js_config()->get_child_int("base_latency");
    this->cluster_stride = this->get_js_config()->get_child_int("cluster_stride");

    this->regs_itf.set_req_meth(&VelocityDma::regs_req);
    this->new_slave_port("regs", &this->regs_itf);

    this->remote_in_itf.set_req_meth(&VelocityDma::remote_req);
    this->new_slave_port("remote_in", &this->remote_in_itf);

    this->new_master_port("tcdm", &this->tcdm_itf);
    this->remote_out_itf.set_grant_meth(&VelocityDma::remote_grant);
    this->remote_out_itf.set_resp_meth(&VelocityDma::remote_resp);
    this->new_master_port("remote_out", &this->remote_out_itf);
}

void VelocityDma::trace_flow(const char *event, const char *direction, vp::IoReq *req,
    vp::IoReqStatus status)
{
    velocity::PacketTraceInfo info;
    velocity::packet_trace_decode(req, info);

    char id[96];
    char src[16];
    char dst[16];
    char root[16];
    velocity::packet_trace_format_id(info, id, sizeof(id));
    velocity::packet_trace_format_node(info.has_src, info.src_cluster, src, sizeof(src));
    velocity::packet_trace_format_node(info.has_dst, info.dst_cluster, dst, sizeof(dst));
    velocity::packet_trace_format_node(info.has_root, info.root_cluster, root, sizeof(root));

    this->flow.msg(vp::Trace::LEVEL_TRACE,
        "FLOW event=%s dir=%s comp=dma node=%u id=%s kind=%s src=%s dst=%s root=%s action=%s phase=%s op=%s addr=0x%llx size=%u payload=%u bytes=%u status=%s pending=%zu\n",
        event, direction, this->cluster_id, id, velocity::packet_trace_kind_name(info.kind),
        src, dst, root, velocity::packet_trace_action_name(info),
        info.kind == velocity::PacketTraceInfo::KIND_DMA ? velocity::packet_phase_name(info.phase) : "none",
        info.kind == velocity::PacketTraceInfo::KIND_COLLECTIVE ? velocity::innetwork_op_name(info.op) : "none",
        (unsigned long long)info.addr, info.req_size, info.payload_size, info.bytes,
        velocity::io_status_name(status), this->remote_pending.size());
}

bool VelocityDma::read_u32(vp::IoReq *req, uint32_t value)
{
    if (req->get_size() != 4)
    {
        return false;
    }

    *(uint32_t *)req->get_data() = value;
    return true;
}

bool VelocityDma::write_u32(vp::IoReq *req, uint32_t &reg)
{
    if (req->get_size() != 4)
    {
        return false;
    }

    reg = *(uint32_t *)req->get_data();
    return true;
}

uint32_t VelocityDma::allocate_txn_id()
{
    for (uint32_t attempt = 0; attempt < UINT32_MAX; attempt++)
    {
        uint32_t id = this->next_txn_id++;
        if (id == 0)
        {
            this->next_txn_id = 1;
            continue;
        }
        if (this->next_txn_id == 0)
        {
            this->next_txn_id = 1;
        }
        if (this->txns.find(id) == this->txns.end())
        {
            return id;
        }
    }

    return 0;
}

uint32_t VelocityDma::get_status(uint32_t txn_id)
{
    auto it = this->txns.find(txn_id);
    if (it == this->txns.end())
    {
        return DMA_STATUS_IDLE;
    }

    return it->second->status;
}

bool VelocityDma::validate_common(Txn *txn)
{
    if (txn->remote_cluster >= this->num_cluster)
    {
        txn->error = DMA_ERROR_BAD_CLUSTER;
        return false;
    }
    if (txn->type != DMA_TYPE_READ && txn->type != DMA_TYPE_WRITE &&
        txn->type != DMA_TYPE_LATENCY_PROBE)
    {
        txn->error = DMA_ERROR_BAD_TYPE;
        return false;
    }
    if (txn->type == DMA_TYPE_LATENCY_PROBE)
    {
        return true;
    }
    if (txn->size == 0)
    {
        txn->error = DMA_ERROR_BAD_SIZE;
        return false;
    }
    if (txn->local_offset + txn->size > this->tcdm_size)
    {
        txn->error = DMA_ERROR_BAD_LOCAL_OFFSET;
        return false;
    }
    if (txn->remote_offset + txn->size > this->tcdm_size)
    {
        txn->error = DMA_ERROR_BAD_REMOTE_OFFSET;
        return false;
    }
    if (txn->type == DMA_TYPE_READ && txn->size > this->read_buffer_size)
    {
        txn->error = DMA_ERROR_BUFFER_TOO_SMALL;
        return false;
    }
    if (txn->type == DMA_TYPE_WRITE && txn->size > this->write_buffer_size)
    {
        txn->error = DMA_ERROR_BUFFER_TOO_SMALL;
        return false;
    }

    return true;
}

bool VelocityDma::local_access(Txn *txn, const char *context, uint32_t offset, uint32_t size,
    uint8_t *data, bool is_write, uint64_t &latency)
{
    uint32_t packet_id = txn ? txn->id : 0;
    this->trace.msg(vp::Trace::LEVEL_TRACE,
        "[%9u][TCDM]: req_begin context=%s addr=0x%x size=%u is_write=%d\n",
        packet_id, context, offset, size, is_write);

    vp::IoReq req(offset, data, size, is_write);
    req.set_initiator(packet_id);
    vp::IoReqStatus status = this->tcdm_itf.req(&req);
    latency += req.get_full_latency();

    this->trace.msg(vp::Trace::LEVEL_TRACE,
        "[%9u][TCDM]: req_end context=%s addr=0x%x size=%u is_write=%d status=%s latency=%llu\n",
        packet_id, context, offset, size, is_write, velocity::io_status_name(status),
        (unsigned long long)req.get_full_latency());

    if (status != vp::IO_REQ_OK)
    {
        txn->error = DMA_ERROR_LOCAL_ACCESS;
        return false;
    }

    return true;
}

std::unique_ptr<VelocityDma::OutgoingPacket> VelocityDma::build_packet(Txn *txn, uint32_t packet_id,
    uint32_t dst_cluster, uint32_t phase, uint32_t local_offset, uint32_t remote_offset,
    uint32_t payload_size, uint8_t *payload, uint64_t completion_latency)
{
    RemoteHeader header;
    header.magic = REMOTE_MAGIC;
    header.dst_cluster = dst_cluster;
    header.src_cluster = this->cluster_id;
    header.txn_id = packet_id;
    header.phase = phase;
    header.payload_size = payload_size;
    header.local_offset = local_offset;
    header.remote_offset = remote_offset;
    header.network_enter_cycle = 0;
    header.network_exit_cycle = 0;

    std::unique_ptr<OutgoingPacket> packet(new OutgoingPacket());
    packet->txn = txn;
    packet->phase = phase;
    packet->dst_cluster = dst_cluster;
    packet->completion_latency = completion_latency;
    packet->data.resize(sizeof(RemoteHeader) + (payload ? payload_size : 0));
    memcpy(packet->data.data(), &header, sizeof(RemoteHeader));
    if (payload)
    {
        memcpy(packet->data.data() + sizeof(RemoteHeader), payload, payload_size);
    }

    this->trace.msg(vp::Trace::LEVEL_TRACE,
        "[%9u][DMA]: build phase=%s src=%u dst=%u local_offset=0x%x remote_offset=0x%x payload_size=%u packet_size=%u\n",
        header.txn_id, velocity::packet_phase_name(phase), header.src_cluster, header.dst_cluster,
        local_offset, remote_offset, payload_size, (uint32_t)packet->data.size());

    return packet;
}

bool VelocityDma::send_packet(std::unique_ptr<OutgoingPacket> packet)
{
    OutgoingPacket *raw = packet.get();
    raw->req.init();
    raw->req.set_addr((uint64_t)raw->dst_cluster * this->cluster_stride);
    raw->req.set_data(raw->data.data());
    raw->req.set_size(raw->data.size());
    raw->req.set_is_write(true);

    RemoteHeader *header = (RemoteHeader *)raw->data.data();
    this->trace.msg(vp::Trace::LEVEL_TRACE,
        "[%9u][DMA]: send_prepare phase=%s src=%u dst=%u addr=0x%llx packet_size=%u\n",
        header->txn_id, velocity::packet_phase_name(header->phase), header->src_cluster,
        header->dst_cluster, (unsigned long long)raw->req.get_addr(), (uint32_t)raw->req.get_size());

    this->remote_pending[&raw->req] = std::move(packet);
    return this->issue_packet(raw);
}

bool VelocityDma::issue_packet(OutgoingPacket *packet)
{
    this->stamp_latency_probe_enter(packet);
    RemoteHeader *header = (RemoteHeader *)packet->data.data();
    this->trace.msg(vp::Trace::LEVEL_TRACE,
        "[%9u][DMA]: send_issue phase=%s src=%u dst=%u addr=0x%llx packet_size=%u\n",
        header->txn_id, velocity::packet_phase_name(header->phase), header->src_cluster,
        header->dst_cluster, (unsigned long long)packet->req.get_addr(), (uint32_t)packet->req.get_size());

    this->trace_flow("enter", "tx", &packet->req, vp::IO_REQ_PENDING);
    vp::IoReqStatus status = this->remote_out_itf.req(&packet->req);
    this->trace.msg(vp::Trace::LEVEL_TRACE,
        "[%9u][DMA]: send_status phase=%s src=%u dst=%u status=%s full_latency=%llu\n",
        header->txn_id, velocity::packet_phase_name(header->phase), header->src_cluster,
        header->dst_cluster, velocity::io_status_name(status),
        (unsigned long long)packet->req.get_full_latency());

    if (status == vp::IO_REQ_OK)
    {
        this->complete_packet(packet, vp::IO_REQ_OK, true);
        return true;
    }
    if (status == vp::IO_REQ_PENDING || status == vp::IO_REQ_DENIED)
    {
        return true;
    }

    this->complete_packet(packet, status, true);
    return false;
}

bool VelocityDma::schedule_packet_send(std::unique_ptr<OutgoingPacket> packet, uint64_t cycles)
{
    RemoteHeader *header = (RemoteHeader *)packet->data.data();
    this->trace.msg(vp::Trace::LEVEL_TRACE,
        "[%9u][DMA]: schedule_send phase=%s src=%u dst=%u delay=%llu\n",
        header->txn_id, velocity::packet_phase_name(header->phase), header->src_cluster,
        header->dst_cluster, (unsigned long long)cycles);

    if (cycles == 0)
    {
        return this->send_packet(std::move(packet));
    }

    OutgoingPacket *raw = packet.get();
    vp::ClockEvent *event = this->event_new(VelocityDma::send_packet_event);
    event->get_args()[0] = raw;
    this->delayed_packets[raw] = std::move(packet);
    event->enqueue(cycles);

    return true;
}

void VelocityDma::complete_packet(OutgoingPacket *packet, vp::IoReqStatus status, bool synchronous)
{
    auto it = this->remote_pending.find(&packet->req);
    if (it == this->remote_pending.end())
    {
        this->last_error = DMA_ERROR_REMOTE_ACCESS;
        return;
    }

    std::unique_ptr<OutgoingPacket> holder = std::move(it->second);
    this->remote_pending.erase(it);

    RemoteHeader *header = (RemoteHeader *)holder->data.data();
    this->trace.msg(vp::Trace::LEVEL_TRACE,
        "[%9u][DMA]: send_complete phase=%s src=%u dst=%u status=%s synchronous=%d full_latency=%llu\n",
        header->txn_id, velocity::packet_phase_name(header->phase), header->src_cluster,
        header->dst_cluster, velocity::io_status_name(status), synchronous,
        (unsigned long long)holder->req.get_full_latency());
    this->trace_flow("exit", "tx", &holder->req, status);

    if (status != vp::IO_REQ_OK)
    {
        if (holder->txn)
        {
            this->finish_txn(holder->txn, DMA_STATUS_ERROR, DMA_ERROR_REMOTE_ACCESS);
        }
        else if (holder->blocking_op)
        {
            holder->blocking_op->error = DMA_ERROR_REMOTE_ACCESS;
            holder->blocking_op->send_done = true;
            this->complete_two_sided_op(holder->blocking_op);
        }
        else if (holder->collective_op)
        {
            holder->collective_op->error = DMA_ERROR_REMOTE_ACCESS;
            holder->collective_op->send_done = true;
            this->complete_collective_op(holder->collective_op);
        }
        else
        {
            this->last_error = DMA_ERROR_REMOTE_ACCESS;
        }
        return;
    }

    if (holder->blocking_op && holder->phase == REMOTE_PHASE_TWO_SEND)
    {
        holder->blocking_op->send_done = true;
        this->complete_two_sided_op(holder->blocking_op);
    }
    else if (holder->collective_op && holder->phase == REMOTE_PHASE_INNETWORK_COLLECTIVE)
    {
        holder->collective_op->send_done = true;
        this->complete_collective_op(holder->collective_op);
    }
    else if (holder->txn && holder->phase == REMOTE_PHASE_WRITE)
    {
        uint64_t latency = holder->completion_latency;
        if (synchronous)
        {
            latency += holder->req.get_full_latency();
        }
        this->schedule_completion(holder->txn, latency);
    }
    else if (holder->txn && holder->phase == REMOTE_PHASE_LATENCY_PROBE)
    {
        this->record_latency_probe(holder.get());
        this->schedule_completion(holder->txn, 0);
    }
}

void VelocityDma::schedule_remote_write_response(Txn *txn, vp::IoReq *req, uint64_t cycles)
{
    velocity::PacketTraceInfo info;
    if (velocity::packet_trace_decode(req, info))
    {
        this->trace.msg(vp::Trace::LEVEL_TRACE,
            "[%9u][DMA]: schedule_remote_response phase=%s src=%u dst=%u delay=%llu\n",
            info.packet_id, velocity::packet_phase_name(info.phase), info.src_cluster, info.dst_cluster,
            (unsigned long long)(cycles + this->base_latency));
    }

    std::unique_ptr<PendingRemoteWrite> pending(new PendingRemoteWrite());
    PendingRemoteWrite *raw = pending.get();

    pending->txn = txn;
    pending->req = req;
    pending->event = this->event_new(VelocityDma::remote_write_event);
    pending->event->get_args()[0] = raw;

    cycles = std::max<uint64_t>(cycles + this->base_latency, 1);
    this->pending_remote_writes[raw] = std::move(pending);
    raw->event->enqueue(cycles);
}

bool VelocityDma::submit_remote_write(Txn *txn, uint64_t latency)
{
    std::unique_ptr<OutgoingPacket> packet = this->build_packet(txn, txn->id, txn->remote_cluster,
        REMOTE_PHASE_WRITE, txn->local_offset, txn->remote_offset, txn->size,
        txn->buffer.data(), 0);
    return this->schedule_packet_send(std::move(packet), latency);
}

bool VelocityDma::submit_remote_read_req(Txn *txn, uint64_t latency)
{
    std::unique_ptr<OutgoingPacket> packet = this->build_packet(txn, txn->id, txn->remote_cluster,
        REMOTE_PHASE_READ_REQ, txn->local_offset, txn->remote_offset, txn->size, NULL,
        latency);
    return this->send_packet(std::move(packet));
}

bool VelocityDma::submit_remote_read_resp(uint32_t dst_cluster, uint32_t txn_id, uint32_t local_offset,
    uint32_t remote_offset, uint32_t size, uint8_t *payload, uint64_t latency)
{
    std::unique_ptr<OutgoingPacket> packet = this->build_packet(NULL, txn_id, dst_cluster,
        REMOTE_PHASE_READ_RESP, local_offset, remote_offset, size, payload, latency);

    return this->schedule_packet_send(std::move(packet), latency);
}

bool VelocityDma::submit_latency_probe(Txn *txn)
{
    std::unique_ptr<OutgoingPacket> packet = this->build_packet(txn, txn->id, txn->remote_cluster,
        REMOTE_PHASE_LATENCY_PROBE, 0, 0, 0, NULL, 0);
    return this->send_packet(std::move(packet));
}

void VelocityDma::stamp_latency_probe_enter(OutgoingPacket *packet)
{
    if (packet->phase != REMOTE_PHASE_LATENCY_PROBE)
    {
        return;
    }

    RemoteHeader *header = (RemoteHeader *)packet->data.data();
    header->network_enter_cycle = this->clock.get_cycles();
    header->network_exit_cycle = 0;
    this->trace.msg(vp::Trace::LEVEL_TRACE,
        "[%9u][DMA]: latency_probe_enter phase=%s src=%u dst=%u enter_cycle=%llu\n",
        header->txn_id, velocity::packet_phase_name(header->phase), header->src_cluster,
        header->dst_cluster, (unsigned long long)header->network_enter_cycle);
}

void VelocityDma::record_latency_probe(OutgoingPacket *packet)
{
    RemoteHeader *header = (RemoteHeader *)packet->data.data();
    this->last_probe_dst = header->dst_cluster;
    this->last_probe_enter_cycle = header->network_enter_cycle;
    this->last_probe_exit_cycle = header->network_exit_cycle;
    this->last_probe_latency = header->network_exit_cycle >= header->network_enter_cycle ?
        header->network_exit_cycle - header->network_enter_cycle : 0;
    this->trace.msg(vp::Trace::LEVEL_TRACE,
        "[%9u][DMA]: latency_probe_record phase=%s src=%u dst=%u latency=%llu\n",
        header->txn_id, velocity::packet_phase_name(header->phase), header->src_cluster,
        header->dst_cluster, (unsigned long long)this->last_probe_latency);
}

bool VelocityDma::validate_two_sided(BlockingOp *op)
{
    if (op->peer_cluster >= this->num_cluster || op->peer_cluster == this->cluster_id)
    {
        op->error = DMA_ERROR_BAD_CLUSTER;
        return false;
    }
    if (op->size == 0)
    {
        op->error = DMA_ERROR_BAD_SIZE;
        return false;
    }
    if (op->wait_send && op->send_offset + op->size > this->tcdm_size)
    {
        op->error = DMA_ERROR_BAD_LOCAL_OFFSET;
        return false;
    }
    if (op->wait_recv && op->recv_offset + op->size > this->tcdm_size)
    {
        op->error = DMA_ERROR_BAD_LOCAL_OFFSET;
        return false;
    }
    if (op->wait_send && op->size > this->write_buffer_size)
    {
        op->error = DMA_ERROR_BUFFER_TOO_SMALL;
        return false;
    }
    if (this->blocking_ops.size() >= this->max_inflight)
    {
        op->error = DMA_ERROR_NO_SLOT;
        return false;
    }

    return true;
}

vp::IoReqStatus VelocityDma::launch_two_sided(vp::IoReq *req, uint64_t command)
{
    if (req->get_size() != 4)
    {
        return vp::IO_REQ_INVALID;
    }

    std::unique_ptr<BlockingOp> holder(new BlockingOp());
    BlockingOp *op = holder.get();
    op->packet_id = this->allocate_txn_id();
    if (op->packet_id == 0)
    {
        op->packet_id = this->next_txn_id++ & CMD_TXN_ID_MASK;
    }
    op->peer_cluster = this->remote_cluster_reg;
    op->send_offset = this->local_offset_reg;
    op->recv_offset = command == REG_TWO_SENDRECV ? this->remote_offset_reg : this->local_offset_reg;
    op->size = this->size_reg;
    op->wait_send = command == REG_TWO_SEND || command == REG_TWO_SENDRECV;
    op->wait_recv = command == REG_TWO_RECV || command == REG_TWO_SENDRECV;
    op->send_done = !op->wait_send;
    op->recv_done = !op->wait_recv;
    op->error = DMA_ERROR_NONE;
    op->regs_req = req;

    this->trace.msg(vp::Trace::LEVEL_TRACE,
        "[%9u][DMA]: two_sided_launch command=%s src=%u peer=%u send_offset=0x%x recv_offset=0x%x size=%u\n",
        op->packet_id,
        command == REG_TWO_SEND ? "send" : command == REG_TWO_RECV ? "recv" : "sendrecv",
        this->cluster_id, op->peer_cluster, op->send_offset, op->recv_offset, op->size);

    if (!this->validate_two_sided(op))
    {
        this->last_error = op->error;
        return vp::IO_REQ_OK;
    }

    this->last_error = DMA_ERROR_NONE;
    this->blocking_ops[op] = std::move(holder);

    if (op->wait_recv)
    {
        this->post_two_sided_recv(op);
    }

    if (op->wait_send && !this->start_two_sided_send(op))
    {
        auto it = this->blocking_ops.find(op);
        if (it != this->blocking_ops.end())
        {
            this->unpost_two_sided_recv(op);
            this->last_error = op->error;
            this->blocking_ops.erase(it);
        }
        return vp::IO_REQ_OK;
    }

    this->complete_two_sided_op(op);
    return vp::IO_REQ_PENDING;
}

void VelocityDma::post_two_sided_recv(BlockingOp *op)
{
    if (this->match_pending_two_sided_send(op))
    {
        return;
    }

    this->trace.msg(vp::Trace::LEVEL_TRACE,
        "[%9u][DMA]: two_sided_recv_post src=%u peer=%u recv_offset=0x%x size=%u\n",
        op->packet_id, this->cluster_id, op->peer_cluster, op->recv_offset, op->size);
    this->posted_two_sided_recvs.push_back(op);
}

void VelocityDma::unpost_two_sided_recv(BlockingOp *op)
{
    auto it = std::find(this->posted_two_sided_recvs.begin(), this->posted_two_sided_recvs.end(), op);
    if (it != this->posted_two_sided_recvs.end())
    {
        this->posted_two_sided_recvs.erase(it);
    }
}

bool VelocityDma::match_pending_two_sided_send(BlockingOp *op)
{
    for (auto it = this->pending_two_sided_sends.begin(); it != this->pending_two_sided_sends.end(); ++it)
    {
        if (it->src_cluster == op->peer_cluster && it->size == op->size)
        {
            vp::IoReq *remote_req = it->req;
            this->pending_two_sided_sends.erase(it);
            return this->consume_two_sided_send(op, remote_req);
        }
    }

    return false;
}

bool VelocityDma::consume_two_sided_send(BlockingOp *op, vp::IoReq *remote_req)
{
    RemoteHeader header;
    memcpy(&header, remote_req->get_data(), sizeof(RemoteHeader));

    if (remote_req->get_size() != sizeof(RemoteHeader) + header.payload_size ||
        header.payload_size != op->size || op->recv_offset + op->size > this->tcdm_size)
    {
        op->error = DMA_ERROR_BAD_SIZE;
        op->recv_done = true;
        remote_req->status = vp::IO_REQ_INVALID;
        remote_req->get_resp_port()->resp(remote_req);
        this->complete_two_sided_op(op);
        return false;
    }

    Txn scratch;
    scratch.id = header.txn_id;
    scratch.type = DMA_TYPE_WRITE;
    scratch.remote_cluster = header.src_cluster;
    scratch.local_offset = op->recv_offset;
    scratch.remote_offset = header.local_offset;
    scratch.size = op->size;
    scratch.status = DMA_STATUS_BUSY;
    scratch.error = DMA_ERROR_NONE;
    scratch.event = NULL;

    uint64_t latency = 0;
    if (!this->local_access(&scratch, "two_sided_dma_to_tcdm", op->recv_offset, op->size,
        remote_req->get_data() + sizeof(RemoteHeader), true, latency))
    {
        op->error = scratch.error;
        op->recv_done = true;
        remote_req->status = vp::IO_REQ_INVALID;
        remote_req->get_resp_port()->resp(remote_req);
        this->complete_two_sided_op(op);
        return false;
    }

    this->trace.msg(vp::Trace::LEVEL_TRACE,
        "[%9u][DMA]: two_sided_recv_match src=%u peer=%u recv_offset=0x%x size=%u delay=%llu\n",
        op->packet_id, this->cluster_id, op->peer_cluster, op->recv_offset, op->size,
        (unsigned long long)(latency + this->base_latency));
    this->schedule_two_sided_recv_completion(op, remote_req, latency);
    return true;
}

bool VelocityDma::start_two_sided_send(BlockingOp *op)
{
    op->send_buffer.resize(op->size);

    Txn scratch;
    scratch.id = op->packet_id;
    scratch.type = DMA_TYPE_WRITE;
    scratch.remote_cluster = op->peer_cluster;
    scratch.local_offset = op->send_offset;
    scratch.remote_offset = 0;
    scratch.size = op->size;
    scratch.status = DMA_STATUS_BUSY;
    scratch.error = DMA_ERROR_NONE;
    scratch.event = NULL;

    uint64_t latency = 0;
    if (!this->local_access(&scratch, "two_sided_tcdm_to_dma", op->send_offset, op->size,
        op->send_buffer.data(), false, latency))
    {
        op->error = scratch.error;
        op->send_done = true;
        return false;
    }

    std::unique_ptr<OutgoingPacket> packet = this->build_packet(NULL, op->packet_id, op->peer_cluster,
        REMOTE_PHASE_TWO_SEND, op->send_offset, 0, op->size, op->send_buffer.data(), 0);
    packet->blocking_op = op;
    return this->schedule_packet_send(std::move(packet), latency);
}

bool VelocityDma::handle_two_sided_send(vp::IoReq *req, const RemoteHeader &header)
{
    if (req->get_size() != sizeof(RemoteHeader) + header.payload_size || header.payload_size == 0)
    {
        this->last_error = DMA_ERROR_BAD_SIZE;
        return false;
    }

    for (auto it = this->posted_two_sided_recvs.begin(); it != this->posted_two_sided_recvs.end(); ++it)
    {
        BlockingOp *op = *it;
        if (op->peer_cluster == header.src_cluster && op->size == header.payload_size)
        {
            this->posted_two_sided_recvs.erase(it);
            return this->consume_two_sided_send(op, req);
        }
    }

    PendingTwoSidedSend pending;
    pending.src_cluster = header.src_cluster;
    pending.size = header.payload_size;
    pending.req = req;
    this->pending_two_sided_sends.push_back(pending);

    this->trace.msg(vp::Trace::LEVEL_TRACE,
        "[%9u][DMA]: two_sided_send_hold dst=%u src=%u size=%u pending=%u\n",
        header.txn_id, this->cluster_id, header.src_cluster, header.payload_size,
        (uint32_t)this->pending_two_sided_sends.size());
    return true;
}

void VelocityDma::schedule_two_sided_recv_completion(BlockingOp *op, vp::IoReq *remote_req, uint64_t cycles)
{
    std::unique_ptr<PendingTwoSidedRecv> pending(new PendingTwoSidedRecv());
    PendingTwoSidedRecv *raw = pending.get();

    pending->op = op;
    pending->remote_req = remote_req;
    pending->event = this->event_new(VelocityDma::two_sided_recv_event);
    pending->event->get_args()[0] = raw;

    cycles = std::max<uint64_t>(cycles + this->base_latency, 1);
    this->pending_two_sided_recvs[raw] = std::move(pending);
    raw->event->enqueue(cycles);
}

void VelocityDma::complete_two_sided_op(BlockingOp *op)
{
    if ((op->wait_send && !op->send_done) || (op->wait_recv && !op->recv_done))
    {
        return;
    }

    this->trace.msg(vp::Trace::LEVEL_TRACE,
        "[%9u][DMA]: two_sided_complete src=%u peer=%u wait_send=%d wait_recv=%d error=%u\n",
        op->packet_id, this->cluster_id, op->peer_cluster, op->wait_send, op->wait_recv, op->error);

    this->last_error = op->error;
    if (op->regs_req)
    {
        op->regs_req->status = vp::IO_REQ_OK;
        op->regs_req->get_resp_port()->resp(op->regs_req);
        op->regs_req = NULL;
    }

    this->blocking_ops.erase(op);
}

uint32_t VelocityDma::collective_payload_size(const velocity::InNetworkHeader &header)
{
    uint32_t group_count = velocity::innetwork_group_count(header, this->num_cluster);
    if ((header.flags & velocity::INNETWORK_FLAG_DIRECT) != 0 ||
        header.op == velocity::INNETWORK_OP_BROADCAST ||
        header.op == velocity::INNETWORK_OP_REDUCE_INT8_SUM ||
        header.op == velocity::INNETWORK_OP_GATHER)
    {
        return header.bytes;
    }
    if (header.op == velocity::INNETWORK_OP_SCATTER ||
        header.op == velocity::INNETWORK_OP_ALLTOALL)
    {
        return header.bytes * group_count;
    }
    return 0;
}

bool VelocityDma::validate_collective(CollectiveOp *op)
{
    uint32_t group_count = velocity::innetwork_group_count(op->header, this->num_cluster);
    if (group_count == 0 ||
        !velocity::innetwork_group_contains(op->header, this->cluster_id, this->num_cluster) ||
        op->header.root_cluster >= this->num_cluster ||
        !velocity::innetwork_group_contains(op->header, op->header.root_cluster, this->num_cluster))
    {
        op->error = DMA_ERROR_BAD_CLUSTER;
        return false;
    }

    if (op->header.bytes == 0)
    {
        op->error = DMA_ERROR_BAD_SIZE;
        return false;
    }

    if (op->wait_send && op->send_size != 0 && op->send_offset + op->send_size > this->tcdm_size)
    {
        op->error = DMA_ERROR_BAD_LOCAL_OFFSET;
        return false;
    }
    if (op->wait_recv && op->recv_size != 0 && op->recv_offset + op->recv_size > this->tcdm_size)
    {
        op->error = DMA_ERROR_BAD_REMOTE_OFFSET;
        return false;
    }
    if (op->wait_send && op->send_size > this->write_buffer_size)
    {
        op->error = DMA_ERROR_BUFFER_TOO_SMALL;
        return false;
    }
    if (this->collective_ops.size() >= this->max_inflight)
    {
        op->error = DMA_ERROR_NO_SLOT;
        return false;
    }

    return true;
}

bool VelocityDma::start_collective_send(CollectiveOp *op)
{
    op->send_buffer.resize(sizeof(velocity::InNetworkHeader) + op->send_size);
    memcpy(op->send_buffer.data(), &op->header, sizeof(op->header));

    Txn scratch;
    scratch.id = op->packet_id;
    scratch.type = DMA_TYPE_WRITE;
    scratch.remote_cluster = op->header.root_cluster;
    scratch.local_offset = op->send_offset;
    scratch.remote_offset = op->recv_offset;
    scratch.size = op->send_size;
    scratch.status = DMA_STATUS_BUSY;
    scratch.error = DMA_ERROR_NONE;
    scratch.event = NULL;

    uint64_t latency = 0;
    if (!this->local_access(&scratch, "collective_tcdm_to_dma", op->send_offset, op->send_size,
        op->send_buffer.data() + sizeof(velocity::InNetworkHeader), false, latency))
    {
        op->error = scratch.error;
        op->send_done = true;
        return false;
    }

    std::unique_ptr<OutgoingPacket> packet(new OutgoingPacket());
    packet->txn = NULL;
    packet->blocking_op = NULL;
    packet->collective_op = op;
    packet->phase = REMOTE_PHASE_INNETWORK_COLLECTIVE;
    packet->dst_cluster = this->cluster_id;
    packet->completion_latency = 0;
    packet->data = op->send_buffer;
    return this->schedule_packet_send(std::move(packet), latency);
}

bool VelocityDma::collective_headers_match(CollectiveOp *op, const velocity::InNetworkHeader &header)
{
    return op->header.op == header.op &&
        op->header.group_type == header.group_type &&
        op->header.root_cluster == header.root_cluster &&
        op->header.seq == header.seq &&
        op->header.offset == header.offset &&
        op->header.bytes == header.bytes &&
        op->header.group_base == header.group_base &&
        op->header.group_count == header.group_count &&
        op->header.group_stride == header.group_stride &&
        op->header.group_outer_count == header.group_outer_count &&
        op->header.group_outer_stride == header.group_outer_stride;
}

bool VelocityDma::match_collective_packet(
    CollectiveOp *op,
    vp::IoReq *remote_req,
    const velocity::InNetworkHeader &header)
{
    if (!this->collective_headers_match(op, header))
    {
        return false;
    }

    uint32_t payload_size = remote_req->get_size() - sizeof(velocity::InNetworkHeader);
    if (payload_size == 0 || header.offset + op->recv_size > this->tcdm_size)
    {
        op->error = DMA_ERROR_BAD_SIZE;
        return false;
    }

    uint32_t write_offset = op->recv_offset;
    if (header.op == velocity::INNETWORK_OP_ALLTOALL)
    {
        int rank = velocity::innetwork_group_rank(header, header.src_cluster, this->num_cluster);
        if (rank < 0 || payload_size != header.bytes)
        {
            op->error = DMA_ERROR_BAD_SIZE;
            return false;
        }
        write_offset = op->recv_offset + rank * header.bytes;
    }
    else if (payload_size != op->recv_size)
    {
        op->error = DMA_ERROR_BAD_SIZE;
        return false;
    }

    if (write_offset + payload_size > this->tcdm_size)
    {
        op->error = DMA_ERROR_BAD_REMOTE_OFFSET;
        return false;
    }

    Txn scratch;
    scratch.id = header.seq;
    scratch.type = DMA_TYPE_WRITE;
    scratch.remote_cluster = header.src_cluster;
    scratch.local_offset = write_offset;
    scratch.remote_offset = header.offset;
    scratch.size = payload_size;
    scratch.status = DMA_STATUS_BUSY;
    scratch.error = DMA_ERROR_NONE;
    scratch.event = NULL;

    uint64_t latency = 0;
    if (!this->local_access(&scratch, "collective_dma_to_tcdm", write_offset, payload_size,
        remote_req->get_data() + sizeof(velocity::InNetworkHeader), true, latency))
    {
        op->error = scratch.error;
        return false;
    }

    op->received++;
    if (op->received >= op->expected_recv)
    {
        op->recv_done = true;
    }

    this->schedule_remote_write_response(NULL, remote_req, latency);
    this->complete_collective_op(op);
    return true;
}

bool VelocityDma::handle_collective_packet(vp::IoReq *req, const velocity::InNetworkHeader &header)
{
    if (!req->get_is_write() ||
        req->get_size() < sizeof(velocity::InNetworkHeader) + header.bytes ||
        !velocity::innetwork_group_contains(header, this->cluster_id, this->num_cluster))
    {
        this->last_error = DMA_ERROR_REMOTE_ACCESS;
        return false;
    }

    for (auto &entry: this->collective_ops)
    {
        CollectiveOp *op = entry.second.get();
        if (op->wait_recv && !op->recv_done &&
            this->match_collective_packet(op, req, header))
        {
            return true;
        }
    }

    if (this->pending_collective_packets.size() >= this->max_inflight)
    {
        this->trace.fatal("DMA cluster %u collective pending receive overflow\n", this->cluster_id);
    }

    PendingCollectivePacket pending;
    pending.req = req;
    pending.header = header;
    this->pending_collective_packets.push_back(pending);
    this->trace.msg(vp::Trace::LEVEL_TRACE,
        "[%9u][DMA]: collective_hold op=%s dst=%u src=%u pending=%u\n",
        header.seq, velocity::innetwork_op_name(header.op), this->cluster_id, header.src_cluster,
        (uint32_t)this->pending_collective_packets.size());
    return true;
}

vp::IoReqStatus VelocityDma::launch_collective(vp::IoReq *req, uint64_t command)
{
    if (req->get_size() != 4)
    {
        return vp::IO_REQ_INVALID;
    }

    std::unique_ptr<CollectiveOp> holder(new CollectiveOp());
    CollectiveOp *op = holder.get();
    op->packet_id = this->allocate_txn_id();
    op->header.magic = velocity::INNETWORK_MAGIC;
    op->header.offset = this->remote_offset_reg;
    op->header.bytes = this->size_reg;
    op->header.seq = this->coll_seq_reg;
    op->header.root_cluster = this->coll_root_reg;
    op->header.src_cluster = this->cluster_id;
    op->header.op = this->coll_op_reg;
    op->header.group_type = this->coll_group_reg;
    op->header.dtype = 0;
    op->header.flags = 0;
    op->header.group_base = this->coll_group_base_reg;
    op->header.group_count = this->coll_group_count_reg;
    op->header.group_stride = this->coll_group_stride_reg;
    op->header.group_outer_count = this->coll_group_outer_count_reg;
    op->header.group_outer_stride = this->coll_group_outer_stride_reg;
    op->send_offset = this->local_offset_reg;
    op->recv_offset = this->remote_offset_reg;
    op->send_size = this->collective_payload_size(op->header);
    op->recv_size = this->size_reg;
    op->expected_recv = op->header.op == velocity::INNETWORK_OP_ALLTOALL ?
        velocity::innetwork_group_count(op->header, this->num_cluster) : 1;
    if (op->header.op == velocity::INNETWORK_OP_GATHER)
    {
        op->recv_size = this->size_reg * velocity::innetwork_group_count(op->header, this->num_cluster);
    }
    op->wait_send = command == REG_COLL_SEND || command == REG_COLL_SENDRECV;
    op->wait_recv = command == REG_COLL_RECV || command == REG_COLL_SENDRECV;
    op->send_done = !op->wait_send;
    op->recv_done = !op->wait_recv;
    op->received = 0;
    op->error = DMA_ERROR_NONE;
    op->regs_req = req;

    if (!this->validate_collective(op))
    {
        this->last_error = op->error;
        return vp::IO_REQ_OK;
    }

    this->last_error = DMA_ERROR_NONE;
    this->collective_ops[op] = std::move(holder);

    if (op->wait_recv)
    {
        for (auto it = this->pending_collective_packets.begin();
             it != this->pending_collective_packets.end();)
        {
            if (this->match_collective_packet(op, it->req, it->header))
            {
                it = this->pending_collective_packets.erase(it);
                if (this->collective_ops.find(op) == this->collective_ops.end())
                {
                    return vp::IO_REQ_PENDING;
                }
                if (op->recv_done)
                {
                    break;
                }
            }
            else
            {
                ++it;
            }
        }
    }

    if (this->collective_ops.find(op) == this->collective_ops.end())
    {
        return vp::IO_REQ_PENDING;
    }

    if (op->wait_send && !this->start_collective_send(op))
    {
        auto it = this->collective_ops.find(op);
        if (it != this->collective_ops.end())
        {
            this->last_error = op->error;
            this->collective_ops.erase(it);
        }
        return vp::IO_REQ_OK;
    }

    this->complete_collective_op(op);
    return vp::IO_REQ_PENDING;
}

void VelocityDma::complete_collective_op(CollectiveOp *op)
{
    if ((op->wait_send && !op->send_done) || (op->wait_recv && !op->recv_done))
    {
        return;
    }

    this->trace.msg(vp::Trace::LEVEL_TRACE,
        "[%9u][DMA]: collective_complete op=%s cluster=%u error=%u\n",
        op->packet_id, velocity::innetwork_op_name(op->header.op), this->cluster_id, op->error);

    this->last_error = op->error;
    if (op->regs_req)
    {
        op->regs_req->status = vp::IO_REQ_OK;
        op->regs_req->get_resp_port()->resp(op->regs_req);
        op->regs_req = NULL;
    }

    this->collective_ops.erase(op);
}

void VelocityDma::finish_txn(Txn *txn, uint32_t status, uint32_t error)
{
    this->trace.msg(vp::Trace::LEVEL_TRACE,
        "[%9u][DMA]: finish_txn type=%s src=%u dst=%u size=%u status=%s error=%u\n",
        txn->id, dma_type_name(txn->type), this->cluster_id, txn->remote_cluster, txn->size,
        dma_status_name(status), error);

    txn->status = status;
    txn->error = error;
    this->last_error = error;
    this->done_id_reg = txn->id;
    this->remove_inflight(txn);
}

void VelocityDma::remove_inflight(Txn *txn)
{
    auto it = std::find(this->ready.begin(), this->ready.end(), txn);
    if (it != this->ready.end())
    {
        this->ready.erase(it);
    }
}

void VelocityDma::schedule_completion(Txn *txn, uint64_t cycles)
{
    cycles = std::max<uint64_t>(cycles + this->base_latency, 1);
    txn->event = this->event_new(VelocityDma::complete_event);
    txn->event->get_args()[0] = txn;
    txn->event->enqueue(cycles);
}

void VelocityDma::launch_txn(uint32_t requested_id)
{
    if (this->ready.size() >= this->max_inflight)
    {
        this->last_error = DMA_ERROR_NO_SLOT;
        return;
    }

    uint32_t id = requested_id != 0 ? requested_id : this->allocate_txn_id();
    if (id == 0)
    {
        this->last_error = DMA_ERROR_NO_SLOT;
        return;
    }
    if (this->txns.find(id) != this->txns.end())
    {
        this->last_error = DMA_ERROR_DUPLICATE_ID;
        return;
    }

    std::unique_ptr<Txn> holder(new Txn());
    Txn *txn = holder.get();
    txn->id = id;
    txn->type = this->type_reg;
    txn->remote_cluster = this->remote_cluster_reg;
    txn->local_offset = this->local_offset_reg;
    txn->remote_offset = this->remote_offset_reg;
    txn->size = this->size_reg;
    txn->status = DMA_STATUS_BUSY;
    txn->error = DMA_ERROR_NONE;
    txn->buffer.resize(txn->size);
    txn->event = NULL;

    this->txns[id] = std::move(holder);
    this->ready.push_back(txn);
    this->txn_id_reg = id;
    this->last_error = DMA_ERROR_NONE;

    this->trace.msg(vp::Trace::LEVEL_TRACE,
        "[%9u][DMA]: launch type=%s src=%u dst=%u local_offset=0x%x remote_offset=0x%x size=%u\n",
        txn->id, dma_type_name(txn->type), this->cluster_id, txn->remote_cluster,
        txn->local_offset, txn->remote_offset, txn->size);

    if (!this->validate_common(txn))
    {
        this->finish_txn(txn, DMA_STATUS_ERROR, txn->error);
        return;
    }

    uint64_t latency = 0;
    bool ok = false;

    if (txn->type == DMA_TYPE_WRITE)
    {
        ok = this->local_access(txn, "tcdm_to_dma", txn->local_offset, txn->size,
                txn->buffer.data(), false, latency) &&
             this->submit_remote_write(txn, latency);
    }
    else if (txn->type == DMA_TYPE_READ)
    {
        ok = this->submit_remote_read_req(txn, latency);
    }
    else
    {
        ok = this->submit_latency_probe(txn);
    }

    if (!ok)
    {
        this->finish_txn(txn, DMA_STATUS_ERROR, txn->error);
        return;
    }

    if (txn->type == DMA_TYPE_READ)
    {
        return;
    }
}

void VelocityDma::launch_packed_cmd(uint32_t cmd)
{
    this->remote_cluster_reg = cmd & CMD_REMOTE_CLUSTER_MASK;
    this->type_reg = (cmd >> CMD_TYPE_SHIFT) & CMD_TYPE_MASK;
    this->txn_id_reg = (cmd >> CMD_TXN_ID_SHIFT) & CMD_TXN_ID_MASK;
    this->launch_txn(this->txn_id_reg);
}

vp::IoReqStatus VelocityDma::regs_req(vp::Block *__this, vp::IoReq *req)
{
    VelocityDma *_this = (VelocityDma *)__this;
    uint64_t offset = req->get_addr();
    bool ok = true;

    if (req->get_is_write())
    {
        switch (offset)
        {
            case REG_REMOTE_CLUSTER: ok = _this->write_u32(req, _this->remote_cluster_reg); break;
            case REG_LOCAL_OFFSET:   ok = _this->write_u32(req, _this->local_offset_reg); break;
            case REG_REMOTE_OFFSET:  ok = _this->write_u32(req, _this->remote_offset_reg); break;
            case REG_SIZE:           ok = _this->write_u32(req, _this->size_reg); break;
            case REG_TYPE:           ok = _this->write_u32(req, _this->type_reg); break;
            case REG_TXN_ID:         ok = _this->write_u32(req, _this->txn_id_reg); break;
            case REG_QUERY_ID:       ok = _this->write_u32(req, _this->query_id_reg); break;
            case REG_COLL_OP:        ok = _this->write_u32(req, _this->coll_op_reg); break;
            case REG_COLL_GROUP:     ok = _this->write_u32(req, _this->coll_group_reg); break;
            case REG_COLL_ROOT:      ok = _this->write_u32(req, _this->coll_root_reg); break;
            case REG_COLL_SEQ:       ok = _this->write_u32(req, _this->coll_seq_reg); break;
            case REG_COLL_GROUP_BASE:         ok = _this->write_u32(req, _this->coll_group_base_reg); break;
            case REG_COLL_GROUP_COUNT:        ok = _this->write_u32(req, _this->coll_group_count_reg); break;
            case REG_COLL_GROUP_STRIDE:       ok = _this->write_u32(req, _this->coll_group_stride_reg); break;
            case REG_COLL_GROUP_OUTER_COUNT:  ok = _this->write_u32(req, _this->coll_group_outer_count_reg); break;
            case REG_COLL_GROUP_OUTER_STRIDE: ok = _this->write_u32(req, _this->coll_group_outer_stride_reg); break;
            case REG_COLL_SEND:      return _this->launch_collective(req, REG_COLL_SEND);
            case REG_COLL_RECV:      return _this->launch_collective(req, REG_COLL_RECV);
            case REG_COLL_SENDRECV:  return _this->launch_collective(req, REG_COLL_SENDRECV);
            case REG_TWO_SEND:       return _this->launch_two_sided(req, REG_TWO_SEND);
            case REG_TWO_RECV:       return _this->launch_two_sided(req, REG_TWO_RECV);
            case REG_TWO_SENDRECV:   return _this->launch_two_sided(req, REG_TWO_SENDRECV);
            case REG_CMD:
            {
                if (req->get_size() != 4)
                {
                    ok = false;
                    break;
                }
                _this->launch_packed_cmd(*(uint32_t *)req->get_data());
                break;
            }
            case REG_START:
            {
                if (req->get_size() != 4)
                {
                    ok = false;
                    break;
                }
                uint32_t requested_id = *(uint32_t *)req->get_data();
                if (requested_id == 0)
                {
                    requested_id = _this->txn_id_reg;
                }
                _this->launch_txn(requested_id);
                break;
            }
            default:
                ok = false;
                break;
        }
    }
    else
    {
        switch (offset)
        {
            case REG_REMOTE_CLUSTER: ok = _this->read_u32(req, _this->remote_cluster_reg); break;
            case REG_LOCAL_OFFSET:   ok = _this->read_u32(req, _this->local_offset_reg); break;
            case REG_REMOTE_OFFSET:  ok = _this->read_u32(req, _this->remote_offset_reg); break;
            case REG_SIZE:           ok = _this->read_u32(req, _this->size_reg); break;
            case REG_TYPE:           ok = _this->read_u32(req, _this->type_reg); break;
            case REG_TXN_ID:         ok = _this->read_u32(req, _this->txn_id_reg); break;
            case REG_QUERY_ID:       ok = _this->read_u32(req, _this->query_id_reg); break;
            case REG_COLL_OP:        ok = _this->read_u32(req, _this->coll_op_reg); break;
            case REG_COLL_GROUP:     ok = _this->read_u32(req, _this->coll_group_reg); break;
            case REG_COLL_ROOT:      ok = _this->read_u32(req, _this->coll_root_reg); break;
            case REG_COLL_SEQ:       ok = _this->read_u32(req, _this->coll_seq_reg); break;
            case REG_COLL_GROUP_BASE:         ok = _this->read_u32(req, _this->coll_group_base_reg); break;
            case REG_COLL_GROUP_COUNT:        ok = _this->read_u32(req, _this->coll_group_count_reg); break;
            case REG_COLL_GROUP_STRIDE:       ok = _this->read_u32(req, _this->coll_group_stride_reg); break;
            case REG_COLL_GROUP_OUTER_COUNT:  ok = _this->read_u32(req, _this->coll_group_outer_count_reg); break;
            case REG_COLL_GROUP_OUTER_STRIDE: ok = _this->read_u32(req, _this->coll_group_outer_stride_reg); break;
            case REG_STATUS:         ok = _this->read_u32(req, _this->get_status(_this->query_id_reg)); break;
            case REG_DONE_ID:        ok = _this->read_u32(req, _this->done_id_reg); break;
            case REG_ERROR:          ok = _this->read_u32(req, _this->last_error); break;
            case REG_PROBE_ENTER_LO: ok = _this->read_u32(req, _this->last_probe_enter_cycle & 0xffffffff); break;
            case REG_PROBE_ENTER_HI: ok = _this->read_u32(req, _this->last_probe_enter_cycle >> 32); break;
            case REG_PROBE_EXIT_LO:  ok = _this->read_u32(req, _this->last_probe_exit_cycle & 0xffffffff); break;
            case REG_PROBE_EXIT_HI:  ok = _this->read_u32(req, _this->last_probe_exit_cycle >> 32); break;
            case REG_PROBE_LAT_LO:   ok = _this->read_u32(req, _this->last_probe_latency & 0xffffffff); break;
            case REG_PROBE_LAT_HI:   ok = _this->read_u32(req, _this->last_probe_latency >> 32); break;
            case REG_PROBE_DST:      ok = _this->read_u32(req, _this->last_probe_dst); break;
            default:
                ok = false;
                break;
        }
    }

    return ok ? vp::IO_REQ_OK : vp::IO_REQ_INVALID;
}

vp::IoReqStatus VelocityDma::remote_req(vp::Block *__this, vp::IoReq *req)
{
    VelocityDma *_this = (VelocityDma *)__this;
    _this->trace_flow("enter", "rx", req, vp::IO_REQ_PENDING);

    auto finish = [&] (vp::IoReqStatus status) -> vp::IoReqStatus
    {
        if (status != vp::IO_REQ_PENDING)
        {
            _this->trace_flow("exit", "rx", req, status);
        }
        return status;
    };

    if (req->get_size() >= sizeof(velocity::InNetworkHeader))
    {
        velocity::InNetworkHeader collective_header;
        memcpy(&collective_header, req->get_data(), sizeof(collective_header));
        if (collective_header.magic == velocity::INNETWORK_MAGIC)
        {
            return finish(_this->handle_collective_packet(req, collective_header) ?
                vp::IO_REQ_PENDING : vp::IO_REQ_INVALID);
        }
    }

    if (req->get_size() < sizeof(RemoteHeader))
    {
        _this->last_error = DMA_ERROR_BAD_SIZE;
        return finish(vp::IO_REQ_INVALID);
    }

    RemoteHeader header;
    memcpy(&header, req->get_data(), sizeof(RemoteHeader));

    if (header.magic != REMOTE_MAGIC || header.dst_cluster != _this->cluster_id)
    {
        _this->last_error = DMA_ERROR_BAD_DEST;
        return finish(vp::IO_REQ_INVALID);
    }

    if (!req->get_is_write())
    {
        _this->last_error = DMA_ERROR_REMOTE_ACCESS;
        return finish(vp::IO_REQ_INVALID);
    }

    _this->trace.msg(vp::Trace::LEVEL_TRACE,
        "[%9u][DMA]: rx_packet phase=%s src=%u dst=%u payload_size=%u packet_size=%u\n",
        header.txn_id, velocity::packet_phase_name(header.phase), header.src_cluster,
        header.dst_cluster, header.payload_size, (uint32_t)req->get_size());

    switch (header.phase)
    {
        case REMOTE_PHASE_WRITE:
        {
            if (req->get_size() != sizeof(RemoteHeader) + header.payload_size ||
                header.remote_offset + header.payload_size > _this->tcdm_size)
            {
                _this->last_error = DMA_ERROR_BAD_SIZE;
                return finish(vp::IO_REQ_INVALID);
            }

            vp::IoReq tcdm_req(header.remote_offset, req->get_data() + sizeof(RemoteHeader),
                header.payload_size, true);
            tcdm_req.set_initiator(header.txn_id);
            _this->trace.msg(vp::Trace::LEVEL_TRACE,
                "[%9u][TCDM]: req_begin context=remote_dma_to_tcdm addr=0x%x size=%u is_write=1\n",
                header.txn_id, header.remote_offset, header.payload_size);

            vp::IoReqStatus status = _this->tcdm_itf.req(&tcdm_req);
            _this->trace.msg(vp::Trace::LEVEL_TRACE,
                "[%9u][TCDM]: req_end context=remote_dma_to_tcdm addr=0x%x size=%u is_write=1 status=%s latency=%llu\n",
                header.txn_id, header.remote_offset, header.payload_size,
                velocity::io_status_name(status), (unsigned long long)tcdm_req.get_full_latency());

            if (status != vp::IO_REQ_OK)
            {
                _this->last_error = DMA_ERROR_LOCAL_ACCESS;
                return finish(vp::IO_REQ_INVALID);
            }

            _this->schedule_remote_write_response(NULL, req, tcdm_req.get_full_latency());
            return finish(vp::IO_REQ_PENDING);
        }

        case REMOTE_PHASE_READ_REQ:
        {
            if (req->get_size() != sizeof(RemoteHeader) ||
                header.remote_offset + header.payload_size > _this->tcdm_size ||
                header.local_offset + header.payload_size > _this->tcdm_size)
            {
                _this->last_error = DMA_ERROR_BAD_SIZE;
                return finish(vp::IO_REQ_INVALID);
            }

            std::vector<uint8_t> buffer(header.payload_size);
            uint64_t latency = 0;
            Txn scratch;
            scratch.error = DMA_ERROR_NONE;
            scratch.id = header.txn_id;
            scratch.type = DMA_TYPE_READ;
            scratch.remote_cluster = header.src_cluster;
            scratch.local_offset = header.remote_offset;
            scratch.remote_offset = header.local_offset;
            scratch.size = header.payload_size;
            scratch.status = DMA_STATUS_BUSY;
            if (!_this->local_access(&scratch, "remote_tcdm_to_dma", header.remote_offset, header.payload_size,
                buffer.data(), false, latency))
            {
                _this->last_error = scratch.error;
                return finish(vp::IO_REQ_INVALID);
            }

            if (!_this->submit_remote_read_resp(header.src_cluster, header.txn_id,
                header.remote_offset, header.local_offset, header.payload_size, buffer.data(),
                latency + _this->base_latency))
            {
                return finish(vp::IO_REQ_INVALID);
            }

            return finish(vp::IO_REQ_OK);
        }

        case REMOTE_PHASE_READ_RESP:
        {
            if (req->get_size() != sizeof(RemoteHeader) + header.payload_size ||
                header.remote_offset + header.payload_size > _this->tcdm_size)
            {
                _this->last_error = DMA_ERROR_BAD_SIZE;
                return finish(vp::IO_REQ_INVALID);
            }

            auto it = _this->txns.find(header.txn_id);
            if (it == _this->txns.end() || it->second->type != DMA_TYPE_READ)
            {
                _this->last_error = DMA_ERROR_REMOTE_ACCESS;
                return finish(vp::IO_REQ_INVALID);
            }

            Txn *txn = it->second.get();
            uint64_t latency = 0;
            if (!_this->local_access(txn, "dma_to_tcdm", header.remote_offset, header.payload_size,
                req->get_data() + sizeof(RemoteHeader), true, latency))
            {
                _this->finish_txn(txn, DMA_STATUS_ERROR, txn->error);
                return finish(vp::IO_REQ_INVALID);
            }

            _this->schedule_remote_write_response(txn, req, latency);
            return finish(vp::IO_REQ_PENDING);
        }

        case REMOTE_PHASE_LATENCY_PROBE:
        {
            if (req->get_size() != sizeof(RemoteHeader) || header.payload_size != 0)
            {
                _this->last_error = DMA_ERROR_BAD_SIZE;
                return finish(vp::IO_REQ_INVALID);
            }

            header.network_exit_cycle = _this->clock.get_cycles();
            memcpy(req->get_data(), &header, sizeof(RemoteHeader));
            _this->trace.msg(vp::Trace::LEVEL_TRACE,
                "[%9u][DMA]: latency_probe_exit phase=%s src=%u dst=%u enter_cycle=%llu exit_cycle=%llu\n",
                header.txn_id, velocity::packet_phase_name(header.phase), header.src_cluster,
                header.dst_cluster, (unsigned long long)header.network_enter_cycle,
                (unsigned long long)header.network_exit_cycle);
            return finish(vp::IO_REQ_OK);
        }

        case REMOTE_PHASE_TWO_SEND:
        {
            return finish(_this->handle_two_sided_send(req, header) ?
                vp::IO_REQ_PENDING : vp::IO_REQ_INVALID);
        }

        default:
            _this->last_error = DMA_ERROR_REMOTE_ACCESS;
            return finish(vp::IO_REQ_INVALID);
    }
}

void VelocityDma::remote_grant(vp::Block *__this, vp::IoReq *req)
{
    VelocityDma *_this = (VelocityDma *)__this;
    auto it = _this->remote_pending.find(req);
    if (it == _this->remote_pending.end())
    {
        _this->last_error = DMA_ERROR_REMOTE_ACCESS;
        return;
    }

    RemoteHeader *header = (RemoteHeader *)it->second->data.data();
    _this->trace.msg(vp::Trace::LEVEL_TRACE,
        "[%9u][DMA]: send_grant phase=%s src=%u dst=%u\n",
        header->txn_id, velocity::packet_phase_name(header->phase), header->src_cluster,
        header->dst_cluster);
}

void VelocityDma::remote_resp(vp::Block *__this, vp::IoReq *req)
{
    VelocityDma *_this = (VelocityDma *)__this;
    auto it = _this->remote_pending.find(req);
    if (it == _this->remote_pending.end())
    {
        _this->last_error = DMA_ERROR_REMOTE_ACCESS;
        return;
    }

    RemoteHeader *header = (RemoteHeader *)it->second->data.data();
    _this->trace.msg(vp::Trace::LEVEL_TRACE,
        "[%9u][DMA]: send_response phase=%s src=%u dst=%u status=%s\n",
        header->txn_id, velocity::packet_phase_name(header->phase), header->src_cluster,
        header->dst_cluster, velocity::io_status_name(req->status));

    _this->complete_packet(it->second.get(), req->status, false);
}

void VelocityDma::send_packet_event(vp::Block *__this, vp::ClockEvent *event)
{
    VelocityDma *_this = (VelocityDma *)__this;
    OutgoingPacket *packet = (OutgoingPacket *)event->get_args()[0];
    auto it = _this->delayed_packets.find(packet);
    if (it == _this->delayed_packets.end())
    {
        _this->last_error = DMA_ERROR_REMOTE_ACCESS;
        return;
    }

    std::unique_ptr<OutgoingPacket> holder = std::move(it->second);
    _this->delayed_packets.erase(it);
    RemoteHeader *header = (RemoteHeader *)holder->data.data();
    _this->trace.msg(vp::Trace::LEVEL_TRACE,
        "[%9u][DMA]: scheduled_send_fire phase=%s src=%u dst=%u\n",
        header->txn_id, velocity::packet_phase_name(header->phase), header->src_cluster,
        header->dst_cluster);
    _this->send_packet(std::move(holder));
}

void VelocityDma::remote_write_event(vp::Block *__this, vp::ClockEvent *event)
{
    VelocityDma *_this = (VelocityDma *)__this;
    PendingRemoteWrite *pending = (PendingRemoteWrite *)event->get_args()[0];
    auto it = _this->pending_remote_writes.find(pending);
    if (it == _this->pending_remote_writes.end())
    {
        _this->last_error = DMA_ERROR_REMOTE_ACCESS;
        return;
    }

    std::unique_ptr<PendingRemoteWrite> holder = std::move(it->second);
    _this->pending_remote_writes.erase(it);

    velocity::PacketTraceInfo info;
    if (velocity::packet_trace_decode(holder->req, info))
    {
        _this->trace.msg(vp::Trace::LEVEL_TRACE,
            "[%9u][DMA]: remote_response phase=%s src=%u dst=%u status=ok\n",
            info.packet_id, velocity::packet_phase_name(info.phase), info.src_cluster, info.dst_cluster);
    }

    if (holder->txn)
    {
        _this->finish_txn(holder->txn, DMA_STATUS_DONE, DMA_ERROR_NONE);
    }

    _this->trace_flow("exit", "rx", holder->req, vp::IO_REQ_OK);
    holder->req->status = vp::IO_REQ_OK;
    holder->req->get_resp_port()->resp(holder->req);
}

void VelocityDma::two_sided_recv_event(vp::Block *__this, vp::ClockEvent *event)
{
    VelocityDma *_this = (VelocityDma *)__this;
    PendingTwoSidedRecv *pending = (PendingTwoSidedRecv *)event->get_args()[0];
    auto it = _this->pending_two_sided_recvs.find(pending);
    if (it == _this->pending_two_sided_recvs.end())
    {
        _this->last_error = DMA_ERROR_REMOTE_ACCESS;
        return;
    }

    std::unique_ptr<PendingTwoSidedRecv> holder = std::move(it->second);
    _this->pending_two_sided_recvs.erase(it);

    velocity::PacketTraceInfo info;
    if (velocity::packet_trace_decode(holder->remote_req, info))
    {
        _this->trace.msg(vp::Trace::LEVEL_TRACE,
            "[%9u][DMA]: two_sided_recv_response phase=%s src=%u dst=%u status=ok\n",
            info.packet_id, velocity::packet_phase_name(info.phase), info.src_cluster, info.dst_cluster);
    }

    holder->op->recv_done = true;
    _this->trace_flow("exit", "rx", holder->remote_req, vp::IO_REQ_OK);
    holder->remote_req->status = vp::IO_REQ_OK;
    holder->remote_req->get_resp_port()->resp(holder->remote_req);
    _this->complete_two_sided_op(holder->op);
}

void VelocityDma::complete_event(vp::Block *__this, vp::ClockEvent *event)
{
    VelocityDma *_this = (VelocityDma *)__this;
    Txn *txn = (Txn *)event->get_args()[0];
    _this->trace.msg(vp::Trace::LEVEL_TRACE,
        "[%9u][DMA]: completion_fire type=%s\n",
        txn->id, dma_type_name(txn->type));
    _this->finish_txn(txn, DMA_STATUS_DONE, DMA_ERROR_NONE);
}

extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
    return new VelocityDma(config);
}
