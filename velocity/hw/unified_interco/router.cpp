#include <climits>
#include <cstdint>
#include <deque>
#include <memory>
#include <unordered_set>
#include <vector>

#include <vp/vp.hpp>
#include <vp/itf/io.hpp>

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
        bool stalled = false;
    };

    static vp::IoReqStatus req(vp::Block *__this, vp::IoReq *req, int port);
    static void grant(vp::Block *__this, vp::IoReq *req, int port);
    static void response(vp::Block *__this, vp::IoReq *req, int port);
    static void fsm_handler(vp::Block *__this, vp::ClockEvent *event);

    vp::IoReqStatus handle_req(vp::IoReq *req, int port);
    void grant_waiting_input(InputPort &input);
    void schedule();
    int route(vp::IoReq *req);
    void accept_output_req(int port);
    void finish_req(vp::IoReq *req, vp::IoReqStatus status);

    vp::Trace trace;
    std::vector<vp::IoSlave> input_itfs;
    std::vector<vp::IoMaster> output_itfs;
    std::vector<InputPort> inputs;
    std::vector<OutputPort> outputs;
    std::vector<int> routes;
    std::unordered_set<vp::IoReq *> outstanding;
    vp::ClockEvent fsm_event;

    int router_id;
    int radix;
    int num_cluster;
    uint64_t cluster_stride;
    int64_t max_input_pending_size;
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
    if (this->max_input_pending_size == 0)
    {
        this->max_input_pending_size = INT_MAX;
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
        OutputPort &output = _this->outputs[output_id];
        if (output.stalled || output.pending.empty())
        {
            continue;
        }

        QueuedReq queued = output.pending.front();
        output.pending.pop_front();
        vp::IoReq *req = queued.req;
        req->arg_push((void *)req->get_resp_port());

        _this->outstanding.insert(req);
        _this->trace.msg(vp::Trace::LEVEL_TRACE,
            "router=%d forward input=%d output=%d addr=0x%llx size=%u outstanding=%zu\n",
            _this->router_id, queued.input, output_id,
            (unsigned long long)req->get_addr(), (uint32_t)req->get_size(),
            _this->outstanding.size());
        vp::IoReqStatus status = _this->output_itfs[output_id].req(req);
        bool completed = _this->outstanding.find(req) == _this->outstanding.end();

        if (status == vp::IO_REQ_DENIED)
        {
            _this->trace.msg(vp::Trace::LEVEL_TRACE,
                "router=%d stalled output=%d addr=0x%llx size=%u\n",
                _this->router_id, output_id, (unsigned long long)req->get_addr(),
                (uint32_t)req->get_size());
            output.stalled = true;
            output.stalled_req = req;
        }
        else if (status == vp::IO_REQ_PENDING)
        {
            output.stalled = false;
            output.stalled_req = nullptr;
        }
        else
        {
            output.stalled = false;
            output.stalled_req = nullptr;
            if (!completed)
            {
                _this->outstanding.erase(req);
                req->resp_port = (vp::IoSlave *)req->arg_pop();
                _this->finish_req(req, status);
            }
        }
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
        output.stalled = false;
        output.stalled_req = nullptr;
        _this->schedule();
    }
}

void UnifiedRouter::response(vp::Block *__this, vp::IoReq *req, int port)
{
    UnifiedRouter *_this = (UnifiedRouter *)__this;
    if (_this->outstanding.erase(req) == 0)
    {
        return;
    }

    req->resp_port = (vp::IoSlave *)req->arg_pop();
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
