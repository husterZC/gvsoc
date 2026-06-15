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
        std::vector<uint8_t> expected;
        std::vector<uint8_t> seen;
        uint32_t received = 0;
        uint32_t expected_count = 0;
        uint64_t reserved_size = 0;
    };

    struct CollectiveView
    {
        std::vector<uint8_t> subset;
        const uint8_t *payload = nullptr;
        uint32_t payload_size = 0;
        uint32_t group_count = 0;
        uint32_t subset_count = 0;
    };

    struct CollectiveBucket
    {
        int output = -1;
        uint32_t representative = 0;
        uint32_t count = 0;
        std::vector<uint8_t> subset;
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
    bool has_collective_subtrees() const;
    uint64_t collective_alu_delay(uint32_t bytes);
    uint32_t collective_payload_size(const velocity::InNetworkHeader &header, uint32_t group_count);
    uint32_t collective_subset_payload_size(const velocity::InNetworkHeader &header,
        uint32_t subset_count);
    uint32_t collective_subset_bytes(uint32_t group_count) const;
    bool collective_subset_get(const std::vector<uint8_t> &subset, uint32_t rank) const;
    void collective_subset_set(std::vector<uint8_t> &subset, uint32_t rank) const;
    uint32_t collective_subset_count(const std::vector<uint8_t> &subset, uint32_t group_count) const;
    bool collective_subset_contains_all(const std::vector<uint8_t> &container,
        const std::vector<uint8_t> &subset, uint32_t group_count) const;
    bool collective_subset_overlaps(const std::vector<uint8_t> &left,
        const std::vector<uint8_t> &right, uint32_t group_count) const;
    void collective_subset_or(std::vector<uint8_t> &dst, const std::vector<uint8_t> &src,
        uint32_t group_count) const;
    int collective_subset_packed_index(const std::vector<uint8_t> &subset, uint32_t rank) const;
    std::vector<uint8_t> collective_full_subset(uint32_t group_count) const;
    std::vector<uint8_t> collective_single_rank_subset(uint32_t group_count, uint32_t rank) const;
    bool collective_subtree_contains(uint32_t root_cluster, uint32_t cluster) const;
    std::vector<uint8_t> collective_expected_subset(const velocity::InNetworkHeader &header,
        uint32_t group_count);
    bool collective_decode_packet(vp::IoReq *req, const velocity::InNetworkHeader &header,
        CollectiveView &view);
    std::string collective_key(const velocity::InNetworkHeader &header);
    void collective_check_capacity(uint64_t bytes);
    void collective_release_packet(vp::IoReq *req);
    void collective_queue_packet(const velocity::InNetworkHeader &header, const uint8_t *payload,
        uint32_t payload_size, uint32_t dst_cluster, int input);
    void collective_queue_packet(const velocity::InNetworkHeader &header,
        const std::vector<uint8_t> *subset, const uint8_t *payload, uint32_t payload_size,
        uint32_t dst_cluster, int input);
    vp::IoReqStatus collective_fanout_packet(vp::IoReq *req, const velocity::InNetworkHeader &header,
        int input);
    vp::IoReqStatus collective_route_direct(vp::IoReq *req, const velocity::InNetworkHeader &header,
        int input);
    vp::IoReqStatus collective_tree_aggregate(vp::IoReq *req, const velocity::InNetworkHeader &header,
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
    std::vector<uint32_t> collective_subtree_words;
    std::unordered_map<vp::IoReq *, vp::IoSlave *> outstanding;
    std::unordered_map<vp::IoReq *, std::unique_ptr<CollectivePacket>> collective_packets;
    std::unordered_map<std::string, std::unique_ptr<CollectiveState>> collective_states;
    vp::ClockEvent fsm_event;

    int router_id;
    int radix;
    int num_cluster;
    uint64_t cluster_stride;
    int collective_subtree_words_per_root;
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

    this->collective_subtree_words_per_root = 0;
    js::Config *subtree_words_per_root_config =
        this->get_js_config()->get("collective_subtree_words_per_root");
    if (subtree_words_per_root_config)
    {
        this->collective_subtree_words_per_root = subtree_words_per_root_config->get_int();
    }

    js::Config *subtree_words_config = this->get_js_config()->get("collective_subtree_words");
    if (subtree_words_config)
    {
        for (auto word : subtree_words_config->get_elems())
        {
            this->collective_subtree_words.push_back((uint32_t)word->get_int());
        }
    }

    if (this->collective_subtree_words_per_root < 0)
    {
        this->trace.fatal("router=%d invalid collective subtree word count %d\n",
            this->router_id, this->collective_subtree_words_per_root);
    }
    if (!this->collective_subtree_words.empty() &&
        this->collective_subtree_words.size() <
            (size_t)this->num_cluster * (size_t)this->collective_subtree_words_per_root)
    {
        this->trace.fatal("router=%d truncated collective subtree metadata (%zu < %d)\n",
            this->router_id, this->collective_subtree_words.size(),
            this->num_cluster * this->collective_subtree_words_per_root);
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

bool UnifiedRouter::has_collective_subtrees() const
{
    return this->collective_subtree_words_per_root > 0 &&
        this->collective_subtree_words.size() >=
            (size_t)this->num_cluster * (size_t)this->collective_subtree_words_per_root;
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

uint32_t UnifiedRouter::collective_subset_payload_size(
    const velocity::InNetworkHeader &header,
    uint32_t subset_count)
{
    if (header.bytes == 0 || subset_count == 0)
    {
        return 0;
    }

    if (header.op == velocity::INNETWORK_OP_BROADCAST ||
        header.op == velocity::INNETWORK_OP_REDUCE_INT8_SUM)
    {
        return header.bytes;
    }

    if (header.op == velocity::INNETWORK_OP_SCATTER ||
        header.op == velocity::INNETWORK_OP_GATHER ||
        header.op == velocity::INNETWORK_OP_ALLTOALL)
    {
        return header.bytes * subset_count;
    }

    return 0;
}

uint32_t UnifiedRouter::collective_subset_bytes(uint32_t group_count) const
{
    return (group_count + 7) / 8;
}

bool UnifiedRouter::collective_subset_get(
    const std::vector<uint8_t> &subset,
    uint32_t rank) const
{
    uint32_t index = rank / 8;
    return index < subset.size() && ((subset[index] >> (rank % 8)) & 1) != 0;
}

void UnifiedRouter::collective_subset_set(
    std::vector<uint8_t> &subset,
    uint32_t rank) const
{
    uint32_t index = rank / 8;
    if (index < subset.size())
    {
        subset[index] |= (uint8_t)(1u << (rank % 8));
    }
}

uint32_t UnifiedRouter::collective_subset_count(
    const std::vector<uint8_t> &subset,
    uint32_t group_count) const
{
    uint32_t count = 0;
    for (uint32_t rank = 0; rank < group_count; rank++)
    {
        if (this->collective_subset_get(subset, rank))
        {
            count++;
        }
    }
    return count;
}

bool UnifiedRouter::collective_subset_contains_all(
    const std::vector<uint8_t> &container,
    const std::vector<uint8_t> &subset,
    uint32_t group_count) const
{
    for (uint32_t rank = 0; rank < group_count; rank++)
    {
        if (this->collective_subset_get(subset, rank) &&
            !this->collective_subset_get(container, rank))
        {
            return false;
        }
    }
    return true;
}

bool UnifiedRouter::collective_subset_overlaps(
    const std::vector<uint8_t> &left,
    const std::vector<uint8_t> &right,
    uint32_t group_count) const
{
    for (uint32_t rank = 0; rank < group_count; rank++)
    {
        if (this->collective_subset_get(left, rank) &&
            this->collective_subset_get(right, rank))
        {
            return true;
        }
    }
    return false;
}

void UnifiedRouter::collective_subset_or(
    std::vector<uint8_t> &dst,
    const std::vector<uint8_t> &src,
    uint32_t group_count) const
{
    for (uint32_t rank = 0; rank < group_count; rank++)
    {
        if (this->collective_subset_get(src, rank))
        {
            this->collective_subset_set(dst, rank);
        }
    }
}

int UnifiedRouter::collective_subset_packed_index(
    const std::vector<uint8_t> &subset,
    uint32_t rank) const
{
    if (!this->collective_subset_get(subset, rank))
    {
        return -1;
    }

    int index = 0;
    for (uint32_t current = 0; current < rank; current++)
    {
        if (this->collective_subset_get(subset, current))
        {
            index++;
        }
    }
    return index;
}

std::vector<uint8_t> UnifiedRouter::collective_full_subset(uint32_t group_count) const
{
    std::vector<uint8_t> subset(this->collective_subset_bytes(group_count), 0);
    for (uint32_t rank = 0; rank < group_count; rank++)
    {
        this->collective_subset_set(subset, rank);
    }
    return subset;
}

std::vector<uint8_t> UnifiedRouter::collective_single_rank_subset(
    uint32_t group_count,
    uint32_t rank) const
{
    std::vector<uint8_t> subset(this->collective_subset_bytes(group_count), 0);
    if (rank < group_count)
    {
        this->collective_subset_set(subset, rank);
    }
    return subset;
}

bool UnifiedRouter::collective_subtree_contains(uint32_t root_cluster, uint32_t cluster) const
{
    if (!this->has_collective_subtrees() ||
        root_cluster >= (uint32_t)this->num_cluster ||
        cluster >= (uint32_t)this->num_cluster)
    {
        return false;
    }

    uint32_t word = root_cluster * this->collective_subtree_words_per_root + cluster / 32;
    return word < this->collective_subtree_words.size() &&
        ((this->collective_subtree_words[word] >> (cluster % 32)) & 1) != 0;
}

std::vector<uint8_t> UnifiedRouter::collective_expected_subset(
    const velocity::InNetworkHeader &header,
    uint32_t group_count)
{
    if (!this->has_collective_subtrees())
    {
        return this->collective_full_subset(group_count);
    }

    std::vector<uint8_t> subset(this->collective_subset_bytes(group_count), 0);
    for (uint32_t rank = 0; rank < group_count; rank++)
    {
        int member = velocity::innetwork_group_member(header, rank, this->num_cluster);
        if (member < 0)
        {
            continue;
        }
        if (this->collective_subtree_contains(header.root_cluster, (uint32_t)member))
        {
            this->collective_subset_set(subset, rank);
        }
    }
    return subset;
}

bool UnifiedRouter::collective_decode_packet(
    vp::IoReq *req,
    const velocity::InNetworkHeader &header,
    CollectiveView &view)
{
    uint32_t group_count = velocity::innetwork_group_count(header, this->num_cluster);
    if (group_count == 0 || header.bytes == 0 ||
        req->get_size() < sizeof(velocity::InNetworkHeader))
    {
        return false;
    }

    uint32_t payload_offset = sizeof(velocity::InNetworkHeader);
    view.group_count = group_count;

    if ((header.flags & velocity::INNETWORK_FLAG_SUBSET) != 0)
    {
        uint32_t bitmap_size = this->collective_subset_bytes(group_count);
        if ((header.flags & velocity::INNETWORK_FLAG_DIRECT) != 0 ||
            req->get_size() < sizeof(velocity::InNetworkHeader) + bitmap_size)
        {
            return false;
        }

        view.subset.resize(bitmap_size);
        memcpy(view.subset.data(), req->get_data() + payload_offset, bitmap_size);
        payload_offset += bitmap_size;
    }
    else if ((header.flags & velocity::INNETWORK_FLAG_DIRECT) != 0)
    {
        if (header.op != velocity::INNETWORK_OP_REDUCE_INT8_SUM &&
            header.op != velocity::INNETWORK_OP_GATHER)
        {
            return false;
        }

        int rank = velocity::innetwork_group_rank(header, header.src_cluster, this->num_cluster);
        if (rank < 0)
        {
            return false;
        }
        view.subset = this->collective_single_rank_subset(group_count, (uint32_t)rank);
    }
    else if (header.op == velocity::INNETWORK_OP_REDUCE_INT8_SUM ||
             header.op == velocity::INNETWORK_OP_GATHER)
    {
        int rank = velocity::innetwork_group_rank(header, header.src_cluster, this->num_cluster);
        if (rank < 0)
        {
            return false;
        }
        view.subset = this->collective_single_rank_subset(group_count, (uint32_t)rank);
    }
    else
    {
        view.subset = this->collective_full_subset(group_count);
    }

    view.subset_count = this->collective_subset_count(view.subset, group_count);
    if (view.subset_count == 0)
    {
        return false;
    }

    if (req->get_size() < payload_offset)
    {
        return false;
    }

    view.payload = req->get_data() + payload_offset;
    view.payload_size = req->get_size() - payload_offset;

    uint32_t expected_payload = (header.flags & velocity::INNETWORK_FLAG_SUBSET) != 0 ?
        this->collective_subset_payload_size(header, view.subset_count) :
        this->collective_payload_size(header, group_count);
    return view.payload_size == expected_payload;
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
    this->collective_queue_packet(header, nullptr, payload, payload_size, dst_cluster, input);
}

void UnifiedRouter::collective_queue_packet(
    const velocity::InNetworkHeader &header,
    const std::vector<uint8_t> *subset,
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

    velocity::InNetworkHeader packet_header = header;
    uint32_t bitmap_size = 0;
    if (subset != nullptr)
    {
        packet_header.flags |= velocity::INNETWORK_FLAG_SUBSET;
        bitmap_size = subset->size();
    }
    else
    {
        packet_header.flags &= ~velocity::INNETWORK_FLAG_SUBSET;
    }

    uint64_t packet_size = sizeof(velocity::InNetworkHeader) + bitmap_size + payload_size;
    this->collective_check_capacity(packet_size);

    std::unique_ptr<CollectivePacket> packet(new CollectivePacket());
    packet->reserved_size = packet_size;
    packet->data.resize(packet_size);
    memcpy(packet->data.data(), &packet_header, sizeof(packet_header));
    uint32_t payload_offset = sizeof(packet_header);
    if (bitmap_size != 0)
    {
        memcpy(packet->data.data() + payload_offset, subset->data(), bitmap_size);
        payload_offset += bitmap_size;
    }
    if (payload_size != 0)
    {
        memcpy(packet->data.data() + payload_offset, payload, payload_size);
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
        "router=%d collective_queue op=%s dst=%u output=%d subset=%u payload=%u pending=%zu buffer=%lld\n",
        this->router_id, velocity::innetwork_op_name(header.op), dst_cluster, output,
        bitmap_size, payload_size, this->collective_packets.size(),
        (long long)this->collective_buffer_used);
    this->try_forward_output(output);
    this->schedule();
}

vp::IoReqStatus UnifiedRouter::collective_fanout_packet(
    vp::IoReq *req,
    const velocity::InNetworkHeader &header,
    int input)
{
    CollectiveView view;
    if (!this->collective_decode_packet(req, header, view))
    {
        req->status = vp::IO_REQ_INVALID;
        return vp::IO_REQ_INVALID;
    }

    std::vector<CollectiveBucket> buckets;
    for (uint32_t rank = 0; rank < view.group_count; rank++)
    {
        if (!this->collective_subset_get(view.subset, rank))
        {
            continue;
        }

        int member = velocity::innetwork_group_member(header, rank, this->num_cluster);
        if (member < 0)
        {
            req->status = vp::IO_REQ_INVALID;
            return vp::IO_REQ_INVALID;
        }

        int output = this->route_cluster((uint32_t)member);
        if (output < 0)
        {
            req->status = vp::IO_REQ_INVALID;
            return vp::IO_REQ_INVALID;
        }

        CollectiveBucket *bucket = nullptr;
        for (auto &candidate : buckets)
        {
            if (candidate.output == output)
            {
                bucket = &candidate;
                break;
            }
        }
        if (bucket == nullptr)
        {
            CollectiveBucket new_bucket;
            new_bucket.output = output;
            new_bucket.representative = (uint32_t)member;
            new_bucket.subset.resize(this->collective_subset_bytes(view.group_count), 0);
            buckets.push_back(new_bucket);
            bucket = &buckets.back();
        }

        this->collective_subset_set(bucket->subset, rank);
        bucket->count++;
    }

    for (auto &bucket : buckets)
    {
        velocity::InNetworkHeader child = header;
        child.flags &= ~(velocity::INNETWORK_FLAG_DIRECT |
            velocity::INNETWORK_FLAG_FINAL | velocity::INNETWORK_FLAG_SUBSET);

        if (bucket.count == 1)
        {
            uint32_t target_rank = 0;
            for (; target_rank < view.group_count; target_rank++)
            {
                if (this->collective_subset_get(bucket.subset, target_rank))
                {
                    break;
                }
            }

            int member = velocity::innetwork_group_member(header, target_rank, this->num_cluster);
            if (member < 0)
            {
                req->status = vp::IO_REQ_INVALID;
                return vp::IO_REQ_INVALID;
            }

            child.flags |= velocity::INNETWORK_FLAG_DIRECT;
            const uint8_t *child_payload = view.payload;
            if (header.op == velocity::INNETWORK_OP_SCATTER ||
                header.op == velocity::INNETWORK_OP_ALLTOALL)
            {
                int source_index = this->collective_subset_packed_index(view.subset, target_rank);
                if (source_index < 0)
                {
                    req->status = vp::IO_REQ_INVALID;
                    return vp::IO_REQ_INVALID;
                }
                child_payload = view.payload + (uint32_t)source_index * header.bytes;
            }

            this->collective_queue_packet(child, child_payload, header.bytes, (uint32_t)member, input);
            continue;
        }

        std::vector<uint8_t> child_payload;
        const uint8_t *child_payload_data = view.payload;
        uint32_t child_payload_size = header.bytes;
        if (header.op == velocity::INNETWORK_OP_SCATTER ||
            header.op == velocity::INNETWORK_OP_ALLTOALL)
        {
            child_payload_size = header.bytes * bucket.count;
            child_payload.resize(child_payload_size);
            uint32_t child_index = 0;
            for (uint32_t rank = 0; rank < view.group_count; rank++)
            {
                if (!this->collective_subset_get(bucket.subset, rank))
                {
                    continue;
                }
                int source_index = this->collective_subset_packed_index(view.subset, rank);
                if (source_index < 0)
                {
                    req->status = vp::IO_REQ_INVALID;
                    return vp::IO_REQ_INVALID;
                }
                memcpy(child_payload.data() + child_index * header.bytes,
                    view.payload + (uint32_t)source_index * header.bytes, header.bytes);
                child_index++;
            }
            child_payload_data = child_payload.data();
        }

        this->collective_queue_packet(child, &bucket.subset, child_payload_data,
            child_payload_size, bucket.representative, input);
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
        req->get_size() < sizeof(velocity::InNetworkHeader) ||
        (header.flags & velocity::INNETWORK_FLAG_SUBSET) != 0)
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

vp::IoReqStatus UnifiedRouter::collective_tree_aggregate(
    vp::IoReq *req,
    const velocity::InNetworkHeader &header,
    int input)
{
    CollectiveView view;
    if (!this->collective_decode_packet(req, header, view))
    {
        req->status = vp::IO_REQ_INVALID;
        return vp::IO_REQ_INVALID;
    }

    std::vector<uint8_t> expected = this->collective_expected_subset(header, view.group_count);
    uint32_t expected_count = this->collective_subset_count(expected, view.group_count);
    if (expected_count == 0 ||
        !this->collective_subset_contains_all(expected, view.subset, view.group_count))
    {
        req->status = vp::IO_REQ_INVALID;
        return vp::IO_REQ_INVALID;
    }

    std::string key = this->collective_key(header);
    auto state_it = this->collective_states.find(key);
    if (state_it == this->collective_states.end())
    {
        uint32_t aggregate_size = header.op == velocity::INNETWORK_OP_GATHER ?
            expected_count * header.bytes : header.bytes;
        uint64_t reserved_size = aggregate_size + expected.size() * 2;
        this->collective_check_capacity(reserved_size);

        std::unique_ptr<CollectiveState> state(new CollectiveState());
        state->header = header;
        state->header.flags &= ~(velocity::INNETWORK_FLAG_DIRECT |
            velocity::INNETWORK_FLAG_FINAL | velocity::INNETWORK_FLAG_SUBSET);
        state->data.resize(aggregate_size, 0);
        state->expected = expected;
        state->seen.resize(expected.size(), 0);
        state->expected_count = expected_count;
        state->reserved_size = reserved_size;
        this->collective_buffer_used += reserved_size;
        state_it = this->collective_states.emplace(key, std::move(state)).first;
    }

    CollectiveState *state = state_it->second.get();
    if (state->expected_count != expected_count ||
        !this->collective_subset_contains_all(state->expected, view.subset, view.group_count))
    {
        req->status = vp::IO_REQ_INVALID;
        return vp::IO_REQ_INVALID;
    }

    if (this->collective_subset_overlaps(state->seen, view.subset, view.group_count))
    {
        this->trace.fatal("router=%d duplicate collective contribution op=%s seq=%u src=%u\n",
            this->router_id, velocity::innetwork_op_name(header.op), header.seq, header.src_cluster);
    }

    if (header.op == velocity::INNETWORK_OP_REDUCE_INT8_SUM)
    {
        for (uint32_t i = 0; i < header.bytes; i++)
        {
            state->data[i] = (uint8_t)(state->data[i] + view.payload[i]);
        }
    }
    else if (header.op == velocity::INNETWORK_OP_GATHER)
    {
        for (uint32_t rank = 0; rank < view.group_count; rank++)
        {
            if (!this->collective_subset_get(view.subset, rank))
            {
                continue;
            }

            int source_index = this->collective_subset_packed_index(view.subset, rank);
            int target_index = this->collective_subset_packed_index(state->expected, rank);
            if (source_index < 0 || target_index < 0)
            {
                req->status = vp::IO_REQ_INVALID;
                return vp::IO_REQ_INVALID;
            }
            memcpy(state->data.data() + (uint32_t)target_index * header.bytes,
                view.payload + (uint32_t)source_index * header.bytes, header.bytes);
        }
    }
    else
    {
        req->status = vp::IO_REQ_INVALID;
        return vp::IO_REQ_INVALID;
    }

    this->collective_subset_or(state->seen, view.subset, view.group_count);
    state->received += view.subset_count;

    this->trace.msg(vp::Trace::LEVEL_TRACE,
        "router=%d collective_aggregate op=%s seq=%u src=%u count=%u/%u\n",
        this->router_id, velocity::innetwork_op_name(header.op), header.seq, header.src_cluster,
        state->received, state->expected_count);

    if (state->received == state->expected_count)
    {
        velocity::InNetworkHeader complete_header = state->header;
        std::vector<uint8_t> complete_data = state->data;
        std::vector<uint8_t> complete_subset = state->expected;
        bool root_router = this->is_root_router(header);

        this->collective_buffer_used -= state->reserved_size;
        this->collective_states.erase(state_it);

        if (root_router)
        {
            complete_header.src_cluster = header.root_cluster;
            complete_header.flags = velocity::INNETWORK_FLAG_DIRECT | velocity::INNETWORK_FLAG_FINAL;
            this->collective_queue_packet(complete_header, complete_data.data(),
                complete_data.size(), header.root_cluster, input);
        }
        else
        {
            complete_header.src_cluster = header.root_cluster;
            complete_header.flags = 0;
            this->collective_queue_packet(complete_header, &complete_subset,
                complete_data.data(), complete_data.size(), header.root_cluster, input);
        }
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
            return this->collective_tree_aggregate(req, header, port);
        }
        return this->collective_route_direct(req, header, port);
    }

    if (header.op == velocity::INNETWORK_OP_BROADCAST ||
        header.op == velocity::INNETWORK_OP_SCATTER ||
        header.op == velocity::INNETWORK_OP_ALLTOALL)
    {
        return this->collective_fanout_packet(req, header, port);
    }

    if (header.op == velocity::INNETWORK_OP_REDUCE_INT8_SUM ||
        header.op == velocity::INNETWORK_OP_GATHER)
    {
        if (this->has_collective_subtrees() || this->is_root_router(header))
        {
            return this->collective_tree_aggregate(req, header, port);
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
