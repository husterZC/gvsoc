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

    struct OutgoingPacket
    {
        Txn *txn;
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

    static vp::IoReqStatus regs_req(vp::Block *__this, vp::IoReq *req);
    static vp::IoReqStatus remote_req(vp::Block *__this, vp::IoReq *req);
    static void remote_grant(vp::Block *__this, vp::IoReq *req);
    static void remote_resp(vp::Block *__this, vp::IoReq *req);
    static void send_packet_event(vp::Block *__this, vp::ClockEvent *event);
    static void remote_write_event(vp::Block *__this, vp::ClockEvent *event);
    static void complete_event(vp::Block *__this, vp::ClockEvent *event);

    uint32_t allocate_txn_id();
    uint32_t get_status(uint32_t txn_id);
    bool validate_common(Txn *txn);
    bool local_access(Txn *txn, uint32_t offset, uint32_t size, uint8_t *data, bool is_write, uint64_t &latency);
    std::unique_ptr<OutgoingPacket> build_packet(Txn *txn, uint32_t dst_cluster, uint32_t phase,
        uint32_t local_offset, uint32_t remote_offset, uint32_t payload_size, uint8_t *payload,
        uint64_t completion_latency);
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
    void finish_txn(Txn *txn, uint32_t status, uint32_t error);
    void launch_txn(uint32_t requested_id);
    void launch_packed_cmd(uint32_t cmd);
    void schedule_completion(Txn *txn, uint64_t cycles);
    void remove_inflight(Txn *txn);
    bool read_u32(vp::IoReq *req, uint32_t value);
    bool write_u32(vp::IoReq *req, uint32_t &reg);

    vp::Trace trace;
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
};

VelocityDma::VelocityDma(vp::ComponentConf &config)
    : vp::Component(config)
{
    this->traces.new_trace("trace", &this->trace, vp::DEBUG);

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
    for (uint32_t attempt = 0; attempt < CMD_TXN_ID_MASK; attempt++)
    {
        uint32_t id = this->next_txn_id++ & CMD_TXN_ID_MASK;
        if (id == 0)
        {
            this->next_txn_id = 1;
            continue;
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

bool VelocityDma::local_access(Txn *txn, uint32_t offset, uint32_t size, uint8_t *data, bool is_write, uint64_t &latency)
{
    vp::IoReq req(offset, data, size, is_write);
    vp::IoReqStatus status = this->tcdm_itf.req(&req);
    latency += req.get_full_latency();

    if (status != vp::IO_REQ_OK)
    {
        txn->error = DMA_ERROR_LOCAL_ACCESS;
        return false;
    }

    return true;
}

std::unique_ptr<VelocityDma::OutgoingPacket> VelocityDma::build_packet(Txn *txn, uint32_t dst_cluster,
    uint32_t phase, uint32_t local_offset, uint32_t remote_offset, uint32_t payload_size,
    uint8_t *payload, uint64_t completion_latency)
{
    RemoteHeader header;
    header.magic = REMOTE_MAGIC;
    header.dst_cluster = dst_cluster;
    header.src_cluster = this->cluster_id;
    header.txn_id = txn ? txn->id : 0;
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

    this->remote_pending[&raw->req] = std::move(packet);
    return this->issue_packet(raw);
}

bool VelocityDma::issue_packet(OutgoingPacket *packet)
{
    this->stamp_latency_probe_enter(packet);
    vp::IoReqStatus status = this->remote_out_itf.req(&packet->req);
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

    if (status != vp::IO_REQ_OK)
    {
        if (holder->txn)
        {
            this->finish_txn(holder->txn, DMA_STATUS_ERROR, DMA_ERROR_REMOTE_ACCESS);
        }
        else
        {
            this->last_error = DMA_ERROR_REMOTE_ACCESS;
        }
        return;
    }

    if (holder->txn && holder->phase == REMOTE_PHASE_WRITE)
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
    std::unique_ptr<OutgoingPacket> packet = this->build_packet(txn, txn->remote_cluster,
        REMOTE_PHASE_WRITE, txn->local_offset, txn->remote_offset, txn->size, txn->buffer.data(),
        0);
    return this->schedule_packet_send(std::move(packet), latency);
}

bool VelocityDma::submit_remote_read_req(Txn *txn, uint64_t latency)
{
    std::unique_ptr<OutgoingPacket> packet = this->build_packet(txn, txn->remote_cluster,
        REMOTE_PHASE_READ_REQ, txn->local_offset, txn->remote_offset, txn->size, NULL, latency);
    return this->send_packet(std::move(packet));
}

bool VelocityDma::submit_remote_read_resp(uint32_t dst_cluster, uint32_t txn_id, uint32_t local_offset,
    uint32_t remote_offset, uint32_t size, uint8_t *payload, uint64_t latency)
{
    std::unique_ptr<OutgoingPacket> packet = this->build_packet(NULL, dst_cluster,
        REMOTE_PHASE_READ_RESP, local_offset, remote_offset, size, payload, latency);

    RemoteHeader *header = (RemoteHeader *)packet->data.data();
    header->txn_id = txn_id;

    return this->schedule_packet_send(std::move(packet), latency);
}

bool VelocityDma::submit_latency_probe(Txn *txn)
{
    std::unique_ptr<OutgoingPacket> packet = this->build_packet(txn, txn->remote_cluster,
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
}

void VelocityDma::record_latency_probe(OutgoingPacket *packet)
{
    RemoteHeader *header = (RemoteHeader *)packet->data.data();
    this->last_probe_dst = header->dst_cluster;
    this->last_probe_enter_cycle = header->network_enter_cycle;
    this->last_probe_exit_cycle = header->network_exit_cycle;
    this->last_probe_latency = header->network_exit_cycle >= header->network_enter_cycle ?
        header->network_exit_cycle - header->network_enter_cycle : 0;
}

void VelocityDma::finish_txn(Txn *txn, uint32_t status, uint32_t error)
{
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

    if (!this->validate_common(txn))
    {
        this->finish_txn(txn, DMA_STATUS_ERROR, txn->error);
        return;
    }

    uint64_t latency = 0;
    bool ok = false;

    if (txn->type == DMA_TYPE_WRITE)
    {
        ok = this->local_access(txn, txn->local_offset, txn->size, txn->buffer.data(), false, latency) &&
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

    if (req->get_size() < sizeof(RemoteHeader))
    {
        _this->last_error = DMA_ERROR_BAD_SIZE;
        return vp::IO_REQ_INVALID;
    }

    RemoteHeader header;
    memcpy(&header, req->get_data(), sizeof(RemoteHeader));

    if (header.magic != REMOTE_MAGIC || header.dst_cluster != _this->cluster_id)
    {
        _this->last_error = DMA_ERROR_BAD_DEST;
        return vp::IO_REQ_INVALID;
    }

    if (!req->get_is_write())
    {
        _this->last_error = DMA_ERROR_REMOTE_ACCESS;
        return vp::IO_REQ_INVALID;
    }

    switch (header.phase)
    {
        case REMOTE_PHASE_WRITE:
        {
            if (req->get_size() != sizeof(RemoteHeader) + header.payload_size ||
                header.remote_offset + header.payload_size > _this->tcdm_size)
            {
                _this->last_error = DMA_ERROR_BAD_SIZE;
                return vp::IO_REQ_INVALID;
            }

            vp::IoReq tcdm_req(header.remote_offset, req->get_data() + sizeof(RemoteHeader),
                header.payload_size, true);
            vp::IoReqStatus status = _this->tcdm_itf.req(&tcdm_req);
            if (status != vp::IO_REQ_OK)
            {
                _this->last_error = DMA_ERROR_LOCAL_ACCESS;
                return vp::IO_REQ_INVALID;
            }

            _this->schedule_remote_write_response(NULL, req, tcdm_req.get_full_latency());
            return vp::IO_REQ_PENDING;
        }

        case REMOTE_PHASE_READ_REQ:
        {
            if (req->get_size() != sizeof(RemoteHeader) ||
                header.remote_offset + header.payload_size > _this->tcdm_size ||
                header.local_offset + header.payload_size > _this->tcdm_size)
            {
                _this->last_error = DMA_ERROR_BAD_SIZE;
                return vp::IO_REQ_INVALID;
            }

            std::vector<uint8_t> buffer(header.payload_size);
            uint64_t latency = 0;
            Txn scratch;
            scratch.error = DMA_ERROR_NONE;
            if (!_this->local_access(&scratch, header.remote_offset, header.payload_size,
                buffer.data(), false, latency))
            {
                _this->last_error = scratch.error;
                return vp::IO_REQ_INVALID;
            }

            if (!_this->submit_remote_read_resp(header.src_cluster, header.txn_id,
                header.remote_offset, header.local_offset, header.payload_size, buffer.data(),
                latency + _this->base_latency))
            {
                return vp::IO_REQ_INVALID;
            }

            return vp::IO_REQ_OK;
        }

        case REMOTE_PHASE_READ_RESP:
        {
            if (req->get_size() != sizeof(RemoteHeader) + header.payload_size ||
                header.remote_offset + header.payload_size > _this->tcdm_size)
            {
                _this->last_error = DMA_ERROR_BAD_SIZE;
                return vp::IO_REQ_INVALID;
            }

            auto it = _this->txns.find(header.txn_id);
            if (it == _this->txns.end() || it->second->type != DMA_TYPE_READ)
            {
                _this->last_error = DMA_ERROR_REMOTE_ACCESS;
                return vp::IO_REQ_INVALID;
            }

            Txn *txn = it->second.get();
            uint64_t latency = 0;
            if (!_this->local_access(txn, header.remote_offset, header.payload_size,
                req->get_data() + sizeof(RemoteHeader), true, latency))
            {
                _this->finish_txn(txn, DMA_STATUS_ERROR, txn->error);
                return vp::IO_REQ_INVALID;
            }

            _this->schedule_remote_write_response(txn, req, latency);
            return vp::IO_REQ_PENDING;
        }

        case REMOTE_PHASE_LATENCY_PROBE:
        {
            if (req->get_size() != sizeof(RemoteHeader) || header.payload_size != 0)
            {
                _this->last_error = DMA_ERROR_BAD_SIZE;
                return vp::IO_REQ_INVALID;
            }

            header.network_exit_cycle = _this->clock.get_cycles();
            memcpy(req->get_data(), &header, sizeof(RemoteHeader));
            return vp::IO_REQ_OK;
        }

        default:
            _this->last_error = DMA_ERROR_REMOTE_ACCESS;
            return vp::IO_REQ_INVALID;
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

    _this->issue_packet(it->second.get());
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

    if (holder->txn)
    {
        _this->finish_txn(holder->txn, DMA_STATUS_DONE, DMA_ERROR_NONE);
    }

    holder->req->status = vp::IO_REQ_OK;
    holder->req->get_resp_port()->resp(holder->req);
}

void VelocityDma::complete_event(vp::Block *__this, vp::ClockEvent *event)
{
    VelocityDma *_this = (VelocityDma *)__this;
    Txn *txn = (Txn *)event->get_args()[0];
    _this->finish_txn(txn, DMA_STATUS_DONE, DMA_ERROR_NONE);
}

extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
    return new VelocityDma(config);
}
