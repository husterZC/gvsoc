#include <climits>
#include <cstdint>
#include <deque>
#include <unordered_set>

#include <vp/vp.hpp>
#include <vp/itf/io.hpp>

class UnifiedLink : public vp::Component
{
public:
    UnifiedLink(vp::ComponentConf &config);

private:
    struct PendingReq
    {
        vp::IoReq *req;
        int64_t ready_cycle;
    };

    static vp::IoReqStatus req(vp::Block *__this, vp::IoReq *req);
    static void grant(vp::Block *__this, vp::IoReq *req);
    static void response(vp::Block *__this, vp::IoReq *req);
    static void fsm_handler(vp::Block *__this, vp::ClockEvent *event);

    vp::IoReqStatus handle_req(vp::IoReq *req);
    void grant_waiting_inputs();
    void schedule(int64_t delay = 1);
    void finish_req(vp::IoReq *req, vp::IoReqStatus status);
    int64_t transfer_cycles(vp::IoReq *req);

    vp::Trace trace;
    vp::IoSlave input_itf;
    vp::IoMaster output_itf;
    vp::ClockEvent fsm_event;

    std::deque<PendingReq> pending;
    std::deque<vp::IoReq *> denied;
    std::unordered_set<vp::IoReq *> outstanding;
    vp::IoReq *stalled_req = nullptr;
    int64_t pending_size = 0;
    int64_t max_pending_size;
    int64_t latency;
    int64_t width;
    int64_t next_send_cycle = 0;
};

UnifiedLink::UnifiedLink(vp::ComponentConf &config)
    : vp::Component(config), fsm_event(this, UnifiedLink::fsm_handler)
{
    this->traces.new_trace("trace", &this->trace, vp::DEBUG);

    this->latency = this->get_js_config()->get("latency")->get_int();
    this->width = this->get_js_config()->get("width")->get_int();
    this->max_pending_size = this->get_js_config()->get("max_pending_size")->get_int();
    if (this->max_pending_size == 0)
    {
        this->max_pending_size = INT_MAX;
    }

    this->input_itf.set_req_meth(&UnifiedLink::req);
    this->new_slave_port("input", &this->input_itf);

    this->output_itf.set_grant_meth(&UnifiedLink::grant);
    this->output_itf.set_resp_meth(&UnifiedLink::response);
    this->new_master_port("output", &this->output_itf);
}

vp::IoReqStatus UnifiedLink::req(vp::Block *__this, vp::IoReq *req)
{
    return ((UnifiedLink *)__this)->handle_req(req);
}

vp::IoReqStatus UnifiedLink::handle_req(vp::IoReq *req)
{
    if (!this->denied.empty() || this->pending_size + (int64_t)req->get_size() > this->max_pending_size)
    {
        this->denied.push_back(req);
        return vp::IO_REQ_DENIED;
    }

    this->pending.push_back({req, this->clock.get_cycles() + this->latency});
    this->pending_size += req->get_size();
    this->schedule();
    return vp::IO_REQ_PENDING;
}

void UnifiedLink::grant_waiting_inputs()
{
    while (!this->denied.empty() &&
        this->pending_size + (int64_t)this->denied.front()->get_size() <= this->max_pending_size)
    {
        vp::IoReq *req = this->denied.front();
        this->denied.pop_front();
        this->pending.push_back({req, this->clock.get_cycles() + this->latency});
        this->pending_size += req->get_size();
        req->get_resp_port()->grant(req);
    }
}

void UnifiedLink::schedule(int64_t delay)
{
    if (!this->fsm_event.is_enqueued())
    {
        this->fsm_event.enqueue(delay < 1 ? 1 : delay);
    }
}

int64_t UnifiedLink::transfer_cycles(vp::IoReq *req)
{
    if (this->width <= 0)
    {
        return 1;
    }

    int64_t cycles = ((int64_t)req->get_size() + this->width - 1) / this->width;
    return cycles < 1 ? 1 : cycles;
}

void UnifiedLink::fsm_handler(vp::Block *__this, vp::ClockEvent *event)
{
    UnifiedLink *_this = (UnifiedLink *)__this;
    int64_t cycles = _this->clock.get_cycles();

    if (_this->stalled_req == nullptr && _this->pending.empty())
    {
        return;
    }

    vp::IoReq *req = _this->stalled_req;
    if (req == nullptr)
    {
        PendingReq pending = _this->pending.front();
        int64_t ready_cycle = pending.ready_cycle > _this->next_send_cycle ? pending.ready_cycle : _this->next_send_cycle;
        if (cycles < ready_cycle)
        {
            _this->schedule(ready_cycle - cycles);
            return;
        }

        _this->pending.pop_front();
        _this->pending_size -= pending.req->get_size();
        _this->grant_waiting_inputs();
        req = pending.req;
        req->arg_push((void *)req->get_resp_port());
    }

    _this->outstanding.insert(req);
    vp::IoReqStatus status = _this->output_itf.req(req);
    bool completed = _this->outstanding.find(req) == _this->outstanding.end();

    if (status == vp::IO_REQ_DENIED)
    {
        _this->stalled_req = req;
    }
    else if (status == vp::IO_REQ_PENDING)
    {
        _this->stalled_req = nullptr;
        _this->next_send_cycle = cycles + _this->transfer_cycles(req);
    }
    else
    {
        _this->stalled_req = nullptr;
        if (!completed)
        {
            _this->outstanding.erase(req);
            req->resp_port = (vp::IoSlave *)req->arg_pop();
            _this->next_send_cycle = cycles + _this->transfer_cycles(req);
            _this->finish_req(req, status);
        }
    }

    if (_this->stalled_req == nullptr && !_this->pending.empty())
    {
        int64_t next_cycle = _this->pending.front().ready_cycle > _this->next_send_cycle ?
            _this->pending.front().ready_cycle : _this->next_send_cycle;
        _this->schedule(next_cycle - cycles);
    }
}

void UnifiedLink::grant(vp::Block *__this, vp::IoReq *req)
{
    UnifiedLink *_this = (UnifiedLink *)__this;
    if (_this->stalled_req == req)
    {
        _this->schedule();
    }
}

void UnifiedLink::response(vp::Block *__this, vp::IoReq *req)
{
    UnifiedLink *_this = (UnifiedLink *)__this;
    if (_this->outstanding.erase(req) == 0)
    {
        return;
    }

    req->resp_port = (vp::IoSlave *)req->arg_pop();
    req->get_resp_port()->resp(req);
}

void UnifiedLink::finish_req(vp::IoReq *req, vp::IoReqStatus status)
{
    req->status = status;
    req->get_resp_port()->resp(req);
}

extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
    return new UnifiedLink(config);
}
