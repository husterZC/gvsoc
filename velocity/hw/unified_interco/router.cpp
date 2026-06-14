#include <climits>
#include <cstdint>
#include <deque>
#include <cstring>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include <vp/vp.hpp>
#include <vp/itf/io.hpp>

#include "../collective_packet.hpp"

class UnifiedRouter : public vp::Component
{
public:
    UnifiedRouter(vp::ComponentConf &config);

private:
    struct QueuedReq
    {
        vp::IoReq *req;
        int input;
    };

    struct InputPort
    {
        std::deque<vp::IoReq *> pending;
        std::deque<vp::IoReq *> denied;
        int64_t pending_size = 0;
    };

    struct OutputPort
    {
        std::deque<QueuedReq> pending;
        vp::IoReq *stalled_req = nullptr;
        int stalled_input = -1;
        bool stalled = false;
    };

    struct CollectivePacket
    {
        std::vector<uint8_t> data;
        vp::IoReq req;
        uint64_t reserved_size = 0;
    };

    struct CollectiveState
    {
        velocity::InNetworkHeader header;
        std::vector<uint8_t> data;
        std::vector<uint8_t> seen;
        uint32_t received = 0;
        uint64_t reserved_size = 0;
    };

    static vp::IoReqStatus req(vp::Block *__this, vp::IoReq *req, int port);
    static void grant(vp::Block *__this, vp::IoReq *req, int port);
    static void response(vp::Block *__this, vp::IoReq *req, int port);
    static void fsm_handler(vp::Block *__this, vp::ClockEvent *event);

    vp::IoReqStatus handle_req(vp::IoReq *req, int port);
    vp::IoReqStatus handle_collective_req(vp::IoReq *req, int port);
    void grant_waiting_input(InputPort &input);
    void schedule();
    int route(vp::IoReq *req);
    int route_cluster(uint32_t cluster);
    bool is_root_router(const velocity::InNetworkHeader &header);
    uint64_t collective_alu_delay(uint32_t bytes);
    uint32_t collective_payload_size(const velocity::InNetworkHeader &header, uint32_t group_count);
    std::string collective_key(const velocity::InNetworkHeader &header);
    void collective_check_capacity(uint64_t bytes);
    void collective_release_packet(vp::IoReq *req);
    void collective_queue_packet(const velocity::InNetworkHeader &header, const uint8_t *payload,
        uint32_t payload_size, uint32_t dst_cluster, int input);
    vp::IoReqStatus collective_split_packet(vp::IoReq *req, const velocity::InNetworkHeader &header,
        int input);
    vp::IoReqStatus collective_route_direct(vp::IoReq *req, const velocity::InNetworkHeader &header,
        int input);
    vp::IoReqStatus collective_root_aggregate(vp::IoReq *req, const velocity::InNetworkHeader &header,
        int input);
    bool try_forward_output(int output_id);
    void accept_output_req(int port);
    void finish_req(vp::IoReq *req, vp::IoReqStatus status);

    vp::Trace trace;
    std::vector<vp::IoSlave> input_itfs;
    std::vector<vp::IoMaster> output_itfs;
    std::vector<InputPort> inputs;
    std::vector<OutputPort> outputs;
    std::vector<int> routes;
    std::vector<int> output_clusters;
    std::unordered_map<vp::IoReq *, vp::IoSlave *> outstanding;
    std::unordered_map<vp::IoReq *, std::unique_ptr<CollectivePacket>> collective_packets;
    std::unordered_map<std::string, std::unique_ptr<CollectiveState>> collective_states;
    vp::ClockEvent fsm_event;

    int router_id;
    int radix;
    int num_cluster;
    uint64_t cluster_stride;
    int64_t max_input_pending_size;
    int64_t collective_buffer_size;
    int64_t collective_max_pending;
    int64_t collective_alu_count;
    int64_t collective_alu_latency;
    int64_t collective_buffer_used = 0;
};

UnifiedRouter::UnifiedRouter(vp::ComponentConf &config)
    : vp::Component(config), fsm_event(this, UnifiedRouter::fsm_handler)
{
    this->traces.new_trace("trace", &this->trace, vp::DEBUG);

    this->router_id = this->get_js_config()->get("router_id")->get_int();
    this->radix = this->get_js_config()->get("radix")->get_int();
    this->num_cluster = this->get_js_config()->get("num_cluster")->get_int();
    this->cluster_stride = this->get_js_config()->get("cluster_stride")->get_int();
    this->max_input_pending_size = this->get_js_config()->get("max_input_pending_size")->get_int();
    this->collective_buffer_size = this->get_js_config()->get("collective_buffer_size")->get_int();
    this->collective_max_pending = this->get_js_config()->get("collective_max_pending")->get_int();
    this->collective_alu_count = this->get_js_config()->get("collective_alu_count")->get_int();
    this->collective_alu_latency = this->get_js_config()->get("collective_alu_latency")->get_int();
    if (this->max_input_pending_size == 0)
    {
        this->max_input_pending_size = INT_MAX;
    }
    if (this->collective_buffer_size == 0)
    {
        this->collective_buffer_size = INT_MAX;
    }
    if (this->collective_max_pending == 0)
    {
        this->collective_max_pending = INT_MAX;
    }
    if (this->collective_alu_count <= 0)
    {
        this->collective_alu_count = 1;
    }
    if (this->collective_alu_latency <= 0)
    {
        this->collective_alu_latency = 1;
    }

    js::Config *routes_config = this->get_js_config()->get("routes");
    this->routes.resize(this->num_cluster, -1);
    if (routes_config)
    {
        int index = 0;
        for (auto route : routes_config->get_elems())
        {
            if (index < this->num_cluster)
            {
                this->routes[index] = route->get_int();
            }
            index++;
        }
    }

    js::Config *output_clusters_config = this->get_js_config()->get("output_clusters");
    this->output_clusters.resize(this->radix, -1);
    if (output_clusters_config)
    {
        int index = 0;
        for (auto output_cluster : output_clusters_config->get_elems())
        {
            if (index < this->radix)
            {
                this->output_clusters[index] = output_cluster->get_int();
            }
            index++;
        }
    }

    this->input_itfs.resize(this->radix);
    this->output_itfs.resize(this->radix);
    this->inputs.resize(this->radix);
    this->outputs.resize(this->radix);

    for (int i = 0; i < this->radix; i++)
    {
        this->input_itfs[i].set_req_meth_muxed(&UnifiedRouter::req, i);
        this->new_slave_port("in_" + std::to_string(i), &this->input_itfs[i], this);

        this->output_itfs[i].set_grant_meth_muxed(&UnifiedRouter::grant, i);
        this->output_itfs[i].set_resp_meth_muxed(&UnifiedRouter::response, i);
        this->new_master_port("out_" + std::to_string(i), &this->output_itfs[i], this);
    }
}

vp::IoReqStatus UnifiedRouter::req(vp::Block *__this, vp::IoReq *req, int port)
{
    return ((UnifiedRouter *)__this)->handle_req(req, port);
}

vp::IoReqStatus UnifiedRouter::handle_req(vp::IoReq *req, int port)
{
    if (req->get_data() != nullptr && req->get_size() >= sizeof(velocity::InNetworkHeader))
    {
        velocity::InNetworkHeader header;
        memcpy(&header, req->get_data(), sizeof(header));
        if (header.magic == velocity::INNETWORK_MAGIC)
        {
            return this->handle_collective_req(req, port);
        }
    }

    int output = port < 0 || port >= this->radix ? -1 : this->route(req);
    if (output < 0)
    {
        this->trace.msg(vp::Trace::LEVEL_TRACE,
            "router=%d invalid input=%d addr=0x%llx size=%u is_write=%d\n",
            this->router_id, port, (unsigned long long)req->get_addr(),
            (uint32_t)req->get_size(), req->get_is_write());
        req->status = vp::IO_REQ_INVALID;
        return vp::IO_REQ_INVALID;
    }

    InputPort &input = this->inputs[port];
    bool fits = input.pending_size + (int64_t)req->get_size() <= this->max_input_pending_size;
    if (!input.denied.empty() || (!fits && input.pending_size != 0))
    {
        this->trace.msg(vp::Trace::LEVEL_TRACE,
            "router=%d deny input=%d output=%d addr=0x%llx size=%u pending=%lld limit=%lld\n",
            this->router_id, port, output, (unsigned long long)req->get_addr(),
            (uint32_t)req->get_size(), (long long)input.pending_size,
            (long long)this->max_input_pending_size);
        input.denied.push_back(req);
        return vp::IO_REQ_DENIED;
    }

    this->trace.msg(vp::Trace::LEVEL_TRACE,
        "router=%d enqueue input=%d output=%d addr=0x%llx size=%u is_write=%d pending=%lld\n",
        this->router_id, port, output, (unsigned long long)req->get_addr(),
        (uint32_t)req->get_size(), req->get_is_write(), (long long)input.pending_size);
    input.pending.push_back(req);
    input.pending_size += req->get_size();
    this->schedule();
    return vp::IO_REQ_PENDING;
}

int UnifiedRouter::route(vp::IoReq *req)
{
    if (this->cluster_stride == 0)
    {
        return -1;
    }

    uint64_t cluster = req->get_addr() / this->cluster_stride;
    if (cluster >= (uint64_t)this->num_cluster)
    {
        return -1;
    }

    int output = this->routes[cluster];
    if (output < 0 || output >= this->radix)
    {
        return -1;
    }

    return output;
}

int UnifiedRouter::route_cluster(uint32_t cluster)
{
    if (cluster >= (uint32_t)this->num_cluster)
    {
        return -1;
    }

    int output = this->routes[cluster];
    if (output < 0 || output >= this->radix)
    {
        return -1;
    }

    return output;
}

bool UnifiedRouter::is_root_router(const velocity::InNetworkHeader &header)
{
    int output = this->route_cluster(header.root_cluster);
    return output >= 0 && output < (int)this->output_clusters.size() &&
        this->output_clusters[output] == header.root_cluster;
}

uint64_t UnifiedRouter::collective_alu_delay(uint32_t bytes)
{
    uint64_t cycles = (bytes + this->collective_alu_count - 1) / this->collective_alu_count;
    if (cycles == 0)
    {
        cycles = 1;
    }
    return cycles * this->collective_alu_latency;
}

uint32_t UnifiedRouter::collective_payload_size(
    const velocity::InNetworkHeader &header,
    uint32_t group_count)
{
    if (header.bytes == 0)
    {
        return 0;
    }

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

std::string UnifiedRouter::collective_key(const velocity::InNetworkHeader &header)
{
    std::ostringstream stream;
    stream << (uint32_t)header.op << ':' << header.group_type << ':' << header.seq << ':'
           << header.root_cluster << ':' << header.offset << ':' << header.bytes << ':'
           << header.group_base << ':' << header.group_count << ':' << header.group_stride << ':'
           << header.group_outer_count << ':'
           << header.group_outer_stride;
    return stream.str();
}

void UnifiedRouter::collective_check_capacity(uint64_t bytes)
{
    if ((int64_t)this->collective_packets.size() >= this->collective_max_pending)
    {
        this->trace.fatal("router=%d collective pending packet limit exceeded (%zu >= %lld)\n",
            this->router_id, this->collective_packets.size(), (long long)this->collective_max_pending);
    }
    if (this->collective_buffer_used + (int64_t)bytes > this->collective_buffer_size)
    {
        this->trace.fatal("router=%d collective buffer exceeded (%lld + %llu > %lld)\n",
            this->router_id, (long long)this->collective_buffer_used,
            (unsigned long long)bytes, (long long)this->collective_buffer_size);
    }
}

void UnifiedRouter::collective_release_packet(vp::IoReq *req)
{
    auto packet = this->collective_packets.find(req);
    if (packet == this->collective_packets.end())
    {
        return;
    }

    this->collective_buffer_used -= packet->second->reserved_size;
    this->collective_packets.erase(packet);
}

void UnifiedRouter::collective_queue_packet(
    const velocity::InNetworkHeader &header,
    const uint8_t *payload,
    uint32_t payload_size,
    uint32_t dst_cluster,
    int input)
{
    int output = this->route_cluster(dst_cluster);
    if (output < 0)
    {
        this->trace.fatal("router=%d collective route failed op=%s dst=%u\n",
            this->router_id, velocity::innetwork_op_name(header.op), dst_cluster);
    }

    uint64_t packet_size = sizeof(velocity::InNetworkHeader) + payload_size;
    this->collective_check_capacity(packet_size);

    std::unique_ptr<CollectivePacket> packet(new CollectivePacket());
    packet->reserved_size = packet_size;
    packet->data.resize(packet_size);
    memcpy(packet->data.data(), &header, sizeof(header));
    if (payload_size != 0)
    {
        memcpy(packet->data.data() + sizeof(header), payload, payload_size);
    }

    packet->req.init();
    packet->req.set_addr((uint64_t)dst_cluster * this->cluster_stride);
    packet->req.set_size(packet_size);
    packet->req.set_data(packet->data.data());
    packet->req.set_is_write(true);
    packet->req.set_initiator(header.seq);
    if ((header.flags & velocity::INNETWORK_FLAG_FINAL) != 0 &&
        header.op == velocity::INNETWORK_OP_REDUCE_INT8_SUM)
    {
        packet->req.inc_latency(this->collective_alu_delay(payload_size));
    }

    vp::IoReq *raw_req = &packet->req;
    this->collective_buffer_used += packet_size;
    this->collective_packets[raw_req] = std::move(packet);
    this->outputs[output].pending.push_back({raw_req, input});
    this->trace.msg(vp::Trace::LEVEL_TRACE,
        "router=%d collective_queue op=%s dst=%u output=%d payload=%u pending=%zu buffer=%lld\n",
        this->router_id, velocity::innetwork_op_name(header.op), dst_cluster, output,
        payload_size, this->collective_packets.size(), (long long)this->collective_buffer_used);
    this->try_forward_output(output);
    this->schedule();
}

vp::IoReqStatus UnifiedRouter::collective_split_packet(
    vp::IoReq *req,
    const velocity::InNetworkHeader &header,
    int input)
{
    uint32_t group_count = velocity::innetwork_group_count(header, this->num_cluster);
    uint32_t expected_payload = this->collective_payload_size(header, group_count);
    if (group_count == 0 || req->get_size() != sizeof(velocity::InNetworkHeader) + expected_payload)
    {
        req->status = vp::IO_REQ_INVALID;
        return vp::IO_REQ_INVALID;
    }

    const uint8_t *payload = req->get_data() + sizeof(velocity::InNetworkHeader);
    for (uint32_t rank = 0; rank < group_count; rank++)
    {
        int member = velocity::innetwork_group_member(header, rank, this->num_cluster);
        if (member < 0)
        {
            req->status = vp::IO_REQ_INVALID;
            return vp::IO_REQ_INVALID;
        }
        velocity::InNetworkHeader child = header;
        child.flags |= velocity::INNETWORK_FLAG_DIRECT;

        const uint8_t *child_payload = payload;
        uint32_t child_size = header.bytes;
        if (header.op == velocity::INNETWORK_OP_SCATTER ||
            header.op == velocity::INNETWORK_OP_ALLTOALL)
        {
            child_payload = payload + rank * header.bytes;
        }

        this->collective_queue_packet(child, child_payload, child_size, member, input);
    }

    req->status = vp::IO_REQ_OK;
    return vp::IO_REQ_OK;
}

vp::IoReqStatus UnifiedRouter::collective_route_direct(
    vp::IoReq *req,
    const velocity::InNetworkHeader &header,
    int input)
{
    uint64_t cluster = req->get_addr() / this->cluster_stride;
    if (cluster >= (uint64_t)this->num_cluster ||
        req->get_size() < sizeof(velocity::InNetworkHeader))
    {
        req->status = vp::IO_REQ_INVALID;
        return vp::IO_REQ_INVALID;
    }

    uint32_t payload_size = req->get_size() - sizeof(velocity::InNetworkHeader);
    const uint8_t *payload = req->get_data() + sizeof(velocity::InNetworkHeader);
    this->collective_queue_packet(header, payload, payload_size, cluster, input);
    req->status = vp::IO_REQ_OK;
    return vp::IO_REQ_OK;
}

vp::IoReqStatus UnifiedRouter::collective_root_aggregate(
    vp::IoReq *req,
    const velocity::InNetworkHeader &header,
    int input)
{
    uint32_t group_count = velocity::innetwork_group_count(header, this->num_cluster);
    int rank = velocity::innetwork_group_rank(header, header.src_cluster, this->num_cluster);
    if (group_count == 0 || rank < 0 || req->get_size() != sizeof(velocity::InNetworkHeader) + header.bytes)
    {
        req->status = vp::IO_REQ_INVALID;
        return vp::IO_REQ_INVALID;
    }

    const uint8_t *payload = req->get_data() + sizeof(velocity::InNetworkHeader);
    std::string key = this->collective_key(header);
    auto state_it = this->collective_states.find(key);
    if (state_it == this->collective_states.end())
    {
        uint32_t aggregate_size = header.op == velocity::INNETWORK_OP_GATHER ?
            group_count * header.bytes : header.bytes;
        this->collective_check_capacity(aggregate_size);

        std::unique_ptr<CollectiveState> state(new CollectiveState());
        state->header = header;
        state->data.resize(aggregate_size, 0);
        state->seen.resize(group_count, 0);
        state->reserved_size = aggregate_size;
        this->collective_buffer_used += aggregate_size;
        state_it = this->collective_states.emplace(key, std::move(state)).first;
    }

    CollectiveState *state = state_it->second.get();
    if (state->seen[rank])
    {
        this->trace.fatal("router=%d duplicate collective contribution op=%s seq=%u src=%u\n",
            this->router_id, velocity::innetwork_op_name(header.op), header.seq, header.src_cluster);
    }

    state->seen[rank] = 1;
    state->received++;
    if (header.op == velocity::INNETWORK_OP_REDUCE_INT8_SUM)
    {
        for (uint32_t i = 0; i < header.bytes; i++)
        {
            state->data[i] = (uint8_t)(state->data[i] + payload[i]);
        }
    }
    else
    {
        memcpy(state->data.data() + rank * header.bytes, payload, header.bytes);
    }

    this->trace.msg(vp::Trace::LEVEL_TRACE,
        "router=%d collective_aggregate op=%s seq=%u src=%u count=%u/%u\n",
        this->router_id, velocity::innetwork_op_name(header.op), header.seq, header.src_cluster,
        state->received, group_count);

    if (state->received == group_count)
    {
        velocity::InNetworkHeader final_header = header;
        final_header.src_cluster = header.root_cluster;
        final_header.flags |= velocity::INNETWORK_FLAG_DIRECT | velocity::INNETWORK_FLAG_FINAL;
        uint32_t final_size = state->data.size();
        this->collective_queue_packet(final_header, state->data.data(), final_size,
            header.root_cluster, input);
        this->collective_buffer_used -= state->reserved_size;
        this->collective_states.erase(state_it);
    }

    req->status = vp::IO_REQ_OK;
    return vp::IO_REQ_OK;
}

vp::IoReqStatus UnifiedRouter::handle_collective_req(vp::IoReq *req, int port)
{
    velocity::InNetworkHeader header;
    memcpy(&header, req->get_data(), sizeof(header));

    if (port < 0 || port >= this->radix || header.root_cluster >= this->num_cluster ||
        header.src_cluster >= this->num_cluster)
    {
        req->status = vp::IO_REQ_INVALID;
        return vp::IO_REQ_INVALID;
    }

    if ((header.flags & velocity::INNETWORK_FLAG_FINAL) != 0)
    {
        return this->collective_route_direct(req, header, port);
    }

    if ((header.flags & velocity::INNETWORK_FLAG_DIRECT) != 0)
    {
        if ((header.op == velocity::INNETWORK_OP_REDUCE_INT8_SUM ||
             header.op == velocity::INNETWORK_OP_GATHER) &&
            this->is_root_router(header))
        {
            return this->collective_root_aggregate(req, header, port);
        }
        return this->collective_route_direct(req, header, port);
    }

    if (header.op == velocity::INNETWORK_OP_BROADCAST ||
        header.op == velocity::INNETWORK_OP_SCATTER ||
        header.op == velocity::INNETWORK_OP_ALLTOALL)
    {
        return this->collective_split_packet(req, header, port);
    }

    if (header.op == velocity::INNETWORK_OP_REDUCE_INT8_SUM ||
        header.op == velocity::INNETWORK_OP_GATHER)
    {
        if (this->is_root_router(header))
        {
            return this->collective_root_aggregate(req, header, port);
        }

        velocity::InNetworkHeader direct = header;
        direct.flags |= velocity::INNETWORK_FLAG_DIRECT;
        uint32_t payload_size = req->get_size() - sizeof(velocity::InNetworkHeader);
        const uint8_t *payload = req->get_data() + sizeof(velocity::InNetworkHeader);
        this->collective_queue_packet(direct, payload, payload_size, header.root_cluster, port);
        req->status = vp::IO_REQ_OK;
        return vp::IO_REQ_OK;
    }

    req->status = vp::IO_REQ_INVALID;
    return vp::IO_REQ_INVALID;
}

void UnifiedRouter::grant_waiting_input(InputPort &input)
{
    while (!input.denied.empty())
    {
        vp::IoReq *req = input.denied.front();
        bool fits = input.pending_size + (int64_t)req->get_size() <= this->max_input_pending_size;
        if (!fits && input.pending_size != 0)
        {
            break;
        }

        input.denied.pop_front();
        input.pending.push_back(req);
        input.pending_size += req->get_size();
        req->get_resp_port()->grant(req);
    }
}

void UnifiedRouter::schedule()
{
    if (!this->fsm_event.is_enqueued())
    {
        this->fsm_event.enqueue(1);
    }
}

bool UnifiedRouter::try_forward_output(int output_id)
{
    if (output_id < 0 || output_id >= this->radix)
    {
        return false;
    }

    OutputPort &output = this->outputs[output_id];
    if (output.stalled || output.pending.empty())
    {
        return false;
    }

    QueuedReq queued = output.pending.front();
    output.pending.pop_front();
    vp::IoReq *req = queued.req;
    bool is_collective = this->collective_packets.find(req) != this->collective_packets.end();

    if (!is_collective)
    {
        this->outstanding[req] = req->get_resp_port();
    }
    this->trace.msg(vp::Trace::LEVEL_TRACE,
        "router=%d forward input=%d output=%d addr=0x%llx size=%u outstanding=%zu\n",
        this->router_id, queued.input, output_id,
        (unsigned long long)req->get_addr(), (uint32_t)req->get_size(),
        this->outstanding.size() + this->collective_packets.size());
    vp::IoReqStatus status = this->output_itfs[output_id].req(req);
    auto outstanding = this->outstanding.find(req);
    bool completed = is_collective || outstanding == this->outstanding.end();

    if (status == vp::IO_REQ_DENIED)
    {
        this->trace.msg(vp::Trace::LEVEL_TRACE,
            "router=%d stalled output=%d addr=0x%llx size=%u\n",
            this->router_id, output_id, (unsigned long long)req->get_addr(),
            (uint32_t)req->get_size());
        output.stalled = true;
        output.stalled_req = req;
        output.stalled_input = queued.input;
    }
    else if (status == vp::IO_REQ_PENDING)
    {
        output.stalled = false;
        output.stalled_req = nullptr;
        output.stalled_input = -1;
    }
    else
    {
        output.stalled = false;
        output.stalled_req = nullptr;
        output.stalled_input = -1;
        if (is_collective)
        {
            this->collective_release_packet(req);
        }
        else if (!completed)
        {
            req->resp_port = outstanding->second;
            this->outstanding.erase(outstanding);
            this->finish_req(req, status);
        }
    }

    return true;
}

void UnifiedRouter::fsm_handler(vp::Block *__this, vp::ClockEvent *event)
{
    UnifiedRouter *_this = (UnifiedRouter *)__this;

    for (int input_id = 0; input_id < _this->radix; input_id++)
    {
        InputPort &input = _this->inputs[input_id];
        if (input.pending.empty())
        {
            continue;
        }

        vp::IoReq *req = input.pending.front();
        int output = _this->route(req);
        if (output < 0)
        {
            input.pending.pop_front();
            input.pending_size -= req->get_size();
            _this->grant_waiting_input(input);
            _this->trace.msg(vp::Trace::LEVEL_TRACE,
                "router=%d route_invalid input=%d addr=0x%llx size=%u\n",
                _this->router_id, input_id, (unsigned long long)req->get_addr(),
                (uint32_t)req->get_size());
            _this->finish_req(req, vp::IO_REQ_INVALID);
            continue;
        }

        input.pending.pop_front();
        input.pending_size -= req->get_size();
        _this->grant_waiting_input(input);
        _this->outputs[output].pending.push_back({req, input_id});
    }

    for (int output_id = 0; output_id < _this->radix; output_id++)
    {
        _this->try_forward_output(output_id);
    }

    for (int i = 0; i < _this->radix; i++)
    {
        if (!_this->inputs[i].pending.empty() ||
            (!_this->outputs[i].stalled && !_this->outputs[i].pending.empty()))
        {
            _this->schedule();
            return;
        }
    }
}

void UnifiedRouter::grant(vp::Block *__this, vp::IoReq *req, int port)
{
    UnifiedRouter *_this = (UnifiedRouter *)__this;
    if (port < 0 || port >= _this->radix)
    {
        return;
    }

    OutputPort &output = _this->outputs[port];
    if (output.stalled_req == req)
    {
        _this->trace.msg(vp::Trace::LEVEL_TRACE,
            "router=%d grant output=%d addr=0x%llx size=%u\n",
            _this->router_id, port, (unsigned long long)req->get_addr(),
            (uint32_t)req->get_size());
        output.pending.push_front({output.stalled_req, output.stalled_input});
        output.stalled = false;
        output.stalled_req = nullptr;
        output.stalled_input = -1;
        _this->schedule();
    }
}

void UnifiedRouter::response(vp::Block *__this, vp::IoReq *req, int port)
{
    UnifiedRouter *_this = (UnifiedRouter *)__this;
    if (_this->collective_packets.find(req) != _this->collective_packets.end())
    {
        _this->trace.msg(vp::Trace::LEVEL_TRACE,
            "router=%d collective_response port=%d addr=0x%llx size=%u\n",
            _this->router_id, port, (unsigned long long)req->get_addr(),
            (uint32_t)req->get_size());
        _this->collective_release_packet(req);
        return;
    }

    auto outstanding = _this->outstanding.find(req);
    if (outstanding == _this->outstanding.end())
    {
        return;
    }

    req->resp_port = outstanding->second;
    _this->outstanding.erase(outstanding);
    _this->trace.msg(vp::Trace::LEVEL_TRACE,
        "router=%d response port=%d addr=0x%llx size=%u outstanding=%zu\n",
        _this->router_id, port, (unsigned long long)req->get_addr(),
        (uint32_t)req->get_size(), _this->outstanding.size());
    req->get_resp_port()->resp(req);
}

void UnifiedRouter::finish_req(vp::IoReq *req, vp::IoReqStatus status)
{
    this->trace.msg(vp::Trace::LEVEL_TRACE,
        "router=%d finish status=%d addr=0x%llx size=%u\n",
        this->router_id, status, (unsigned long long)req->get_addr(),
        (uint32_t)req->get_size());
    req->status = status;
    req->get_resp_port()->resp(req);
}

extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
    return new UnifiedRouter(config);
}
