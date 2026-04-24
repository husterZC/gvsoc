/*
 * Copyright (C) 2020 GreenWaves Technologies, SAS, ETH Zurich and
 *                    University of Bologna
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

#include <vector>
#include <vp/vp.hpp>
#include <vp/itf/io.hpp>
#include <vp/itf/wire.hpp>
#include <iostream>
#include <string>
#include <tuple>
#include "unified_interconnect/unified_interconnect.hpp"


using namespace std::placeholders;


class ClusterCSR : public vp::Component
{

public:

    ClusterCSR(vp::ComponentConf &config);
    void reset(bool active);
    static vp::IoReqStatus req(vp::Block *__this, vp::IoReq *req);
    static vp::IoReqStatus port_req(vp::Block *__this, vp::IoReq *req);
    void process_reduce(uint32_t addr, uint32_t size, uint32_t level);
    void process_scatter(uint32_t addr, uint32_t size, uint32_t scatter_length);


private:
    static void             lock_sync(vp::Block *__this, bool value);
    static void             send_fsm_handler(vp::Block *__this, vp::ClockEvent *event);
    static void             grant(vp::Block *__this, vp::IoReq *req);
    static void             response(vp::Block *__this, vp::IoReq *req);
    vp::IoReq *             new_req(vp::IoReq *req);
    vp::IoReq *             del_req(vp::IoReq *req);

    vp::Trace               trace;
    vp::IoSlave             in;
    vp::IoSlave             port_in;
    vp::IoMaster            port_out;
    int                     port_granularity;
    std::string             buffer;
    uint32_t                addr_reg;
    uint32_t                size_reg;
    uint32_t                scatter_length_reg;

    vp::WireSlave<bool>     lock_req_itf;
    vp::WireMaster<bool>    lock_ack_itf;
    int                     lock_required;
    int                     lock_granted;

    vp::ClockEvent *        send_fsm_event;
    typedef struct {
        int64_t timestamp;
        bool is_stall;
        vp::IoReq *req;
    } req_buf_t;
    std::queue<req_buf_t>   send_queue;

    std::vector<int64_t>    activity_timestamps;
    enum ActivityType {
        ACTIVITY_WAIT = 0,
        ACTIVITY_PROJ = 1,
        ACTIVITY_ROPE = 2,
        ACTIVITY_NORM = 3,
        ACTIVITY_DOTP = 4,
        ACTIVITY_SCATTER = 5,
        ACTIVITY_REDUCE = 6,
        ACTIVITY_ALLREDUCE = 7,
        ACTIVITY_RESNET = 8,
        ACTIVITY_ACTIVATION = 9,
        ACTIVITY_NUM = 10
    };
    std::vector<std::string> activity_names = {
        "WAIT",
        "PROJ",
        "ROPE",
        "NORM",
        "DOTP",
        "SCATTER",
        "REDUCE",
        "ALLREDUCE",
        "RESNET",
        "ACTIVATION"
    };
};

ClusterCSR::ClusterCSR(vp::ComponentConf &config)
: vp::Component(config)
{
    this->traces.new_trace("trace", &trace, vp::DEBUG);
    this->in.set_req_meth(&ClusterCSR::req);
    this->new_slave_port("input", &this->in);
    this->port_in.set_req_meth(&ClusterCSR::port_req);
    this->new_slave_port("port_in_0", &this->port_in);
    this->new_master_port("port_out_0", &this->port_out);
    this->port_out.set_grant_meth(&ClusterCSR::grant);
    this->port_out.set_resp_meth(&ClusterCSR::response);
    this->port_granularity = this->get_js_config()->get("port_granularity")->get_int();
    this->lock_req_itf.set_sync_meth(&ClusterCSR::lock_sync);
    this->new_slave_port("lock_req", &this->lock_req_itf);
    this->new_master_port("lock_ack", &this->lock_ack_itf);
    this->lock_required = 0;
    this->lock_granted = 0;
    this->send_fsm_event = this->event_new(&ClusterCSR::send_fsm_handler);
    this->activity_timestamps.resize(ACTIVITY_NUM);
}

void ClusterCSR::process_reduce(uint32_t addr, uint32_t size, uint32_t level)
{
    if (level != 0)
    {
        this->trace.fatal("[ClusterCSR] Error: Only support reduction at level 0 for now\n");
    }
    this->trace.msg(vp::Trace::LEVEL_TRACE, "[ClusterCSR] Process reduction request: addr=0x%08x, size=%d\n", addr, size);
    
    int num_flits = (size + this->port_granularity - 1) / this->port_granularity;
    for (int i = 0; i < num_flits; i++)    {
        vp::IoReq *req = new vp::IoReq();
        req->init();
        req->set_addr(addr + i * this->port_granularity);
        req->set_size(this->port_granularity);
        req->set_is_write(0);
        uint8_t * data = new uint8_t[this->port_granularity];
        memset(data, 0, this->port_granularity);
        req->set_data(data);
        *req->arg_get(UnifiedInterconnect::REQ_TXN_TYPE) = (void *)UnifiedInterconnect::TXN_TYPE_REDUCE;
        *req->arg_get(UnifiedInterconnect::REQ_SOUR_ID) = (void *)0;
        *req->arg_get(UnifiedInterconnect::REQ_DEST_ID) = (void *)0;
        *req->arg_get(UnifiedInterconnect::REQ_IS_LAST) = (void *)(i == num_flits - 1 ? 1 : 0);
        *req->arg_get(UnifiedInterconnect::REQ_SIZE) = (void *)this->port_granularity;
        *req->arg_get(UnifiedInterconnect::REQ_ACK) = (void *)0;
        *req->arg_get(UnifiedInterconnect::REQ_PORT_ID) = (void *)0;
        *req->arg_get(UnifiedInterconnect::REQ_COLL_PHASE) = (void *)0;
        *req->arg_get(UnifiedInterconnect::REQ_COLL_LEVEL) = (void *)0;
        *req->arg_get(UnifiedInterconnect::REQ_COLL_SCATTER_SIZE) = (void *)0;
        *req->arg_get(UnifiedInterconnect::REQ_COLL_SCATTER_START) = (void *)0;
        *req->arg_get(UnifiedInterconnect::REQ_COLL_SCATTER_END) = (void *)0;

        req_buf_t buf;
        buf.timestamp = this->clock.get_cycles();
        buf.is_stall = false;
        buf.req = req;
        this->send_queue.push(buf);
    }
}

void ClusterCSR::process_scatter(uint32_t addr, uint32_t size, uint32_t scatter_length)
{
    if (scatter_length == 0)
    {
        this->trace.fatal("[ClusterCSR] Error: Scatter length can not be 0\n");
    }
    if (size < scatter_length)
    {
        this->trace.fatal("[ClusterCSR] Error: Scatter length can not be larger than size\n");
    }
    this->trace.msg(vp::Trace::LEVEL_TRACE, "[ClusterCSR] Process scatter request: addr=0x%08x, size=%d, scatter_length=%d\n", addr, size, scatter_length);

    int scatter_size = size / scatter_length;
    int num_extra_element = 0;
    int extra_size = scatter_size * 2;
    if (scatter_length * scatter_size < size)
    {
        int extra_length = size - scatter_length * scatter_size;
        if (extra_length % scatter_size != 0)
        {
            this->trace.fatal("[ClusterCSR] Error: Scatter size is not aligned with extra length\n");
        }
        num_extra_element = extra_length / scatter_size;
    }
    int num_regular_element = scatter_length - num_extra_element;
    if (num_regular_element * scatter_size + num_extra_element * extra_size != size)
    {
        this->trace.fatal("[ClusterCSR] Error: Something is wrong with the scatter mode calculation\n");
    }
    
    int extra_element_start_id = 0;
    int regular_element_start_id = num_extra_element;
    int per_flit_element = this->port_granularity / extra_size;
    while (num_extra_element > 0)
    {
        int end_id = extra_element_start_id + std::min(num_extra_element, per_flit_element) - 1;
        vp::IoReq *req = new vp::IoReq();
        req->init();
        req->set_addr(addr);
        req->set_size(this->port_granularity);
        req->set_is_write(0);
        uint8_t * data = new uint8_t[this->port_granularity];
        memset(data, 0, this->port_granularity);
        req->set_data(data);
        *req->arg_get(UnifiedInterconnect::REQ_TXN_TYPE) = (void *)UnifiedInterconnect::TXN_TYPE_SCATTER;
        *req->arg_get(UnifiedInterconnect::REQ_SOUR_ID) = (void *)0;
        *req->arg_get(UnifiedInterconnect::REQ_DEST_ID) = (void *)0;
        *req->arg_get(UnifiedInterconnect::REQ_IS_LAST) = (void *)1;
        *req->arg_get(UnifiedInterconnect::REQ_SIZE) = (void *)this->port_granularity;
        *req->arg_get(UnifiedInterconnect::REQ_ACK) = (void *)0;
        *req->arg_get(UnifiedInterconnect::REQ_PORT_ID) = (void *)0;
        *req->arg_get(UnifiedInterconnect::REQ_COLL_PHASE) = (void *)0;
        *req->arg_get(UnifiedInterconnect::REQ_COLL_LEVEL) = (void *)0;
        *req->arg_get(UnifiedInterconnect::REQ_COLL_SCATTER_SIZE) = (void *)extra_size;
        *req->arg_get(UnifiedInterconnect::REQ_COLL_SCATTER_START) = (void *)extra_element_start_id;
        *req->arg_get(UnifiedInterconnect::REQ_COLL_SCATTER_END) = (void *)end_id;

        req_buf_t buf;
        buf.timestamp = this->clock.get_cycles();
        buf.is_stall = false;
        buf.req = req;
        this->send_queue.push(buf);

        extra_element_start_id += std::min(num_extra_element, per_flit_element);
        num_extra_element -= std::min(num_extra_element, per_flit_element);
    }
    
    int per_flit_element = this->port_granularity / scatter_size;
    while (num_regular_element > 0)
    {
        int end_id = regular_element_start_id + std::min(num_regular_element, per_flit_element) - 1;
        vp::IoReq *req = new vp::IoReq();
        req->init();
        req->set_addr(addr);
        req->set_size(this->port_granularity);
        req->set_is_write(0);
        uint8_t * data = new uint8_t[this->port_granularity];
        memset(data, 0, this->port_granularity);
        req->set_data(data);
        *req->arg_get(UnifiedInterconnect::REQ_TXN_TYPE) = (void *)UnifiedInterconnect::TXN_TYPE_SCATTER;
        *req->arg_get(UnifiedInterconnect::REQ_SOUR_ID) = (void *)0;
        *req->arg_get(UnifiedInterconnect::REQ_DEST_ID) = (void *)0;
        *req->arg_get(UnifiedInterconnect::REQ_IS_LAST) = (void *)1;
        *req->arg_get(UnifiedInterconnect::REQ_SIZE) = (void *)this->port_granularity;
        *req->arg_get(UnifiedInterconnect::REQ_ACK) = (void *)0;
        *req->arg_get(UnifiedInterconnect::REQ_PORT_ID) = (void *)0;
        *req->arg_get(UnifiedInterconnect::REQ_COLL_PHASE) = (void *)0;
        *req->arg_get(UnifiedInterconnect::REQ_COLL_LEVEL) = (void *)0;
        *req->arg_get(UnifiedInterconnect::REQ_COLL_SCATTER_SIZE) = (void *)scatter_size;
        *req->arg_get(UnifiedInterconnect::REQ_COLL_SCATTER_START) = (void *)regular_element_start_id;
        *req->arg_get(UnifiedInterconnect::REQ_COLL_SCATTER_END) = (void *)end_id;

        req_buf_t buf;
        buf.timestamp = this->clock.get_cycles();
        buf.is_stall = false;
        buf.req = req;
        this->send_queue.push(buf);

        regular_element_start_id += std::min(num_regular_element, per_flit_element);
        num_regular_element -= std::min(num_regular_element, per_flit_element);
    }
    
}

vp::IoReqStatus ClusterCSR::req(vp::Block *__this, vp::IoReq *req)
{
    ClusterCSR *_this = (ClusterCSR *)__this;
    uint64_t offset = req->get_addr();
    bool is_write = req->get_is_write();
    uint64_t size = req->get_size();
    uint32_t *data = (uint32_t *) req->get_data();

    if (offset == 0)
    {
        // Set Address
        uint32_t value = *(uint32_t *)data;
        _this->addr_reg = value;
    }

    if (offset == 4)
    {
        // Set Size
        uint32_t value = *(uint32_t *)data;
        _this->size_reg = value;
    }

    if (offset == 8)
    {
        uint32_t value = *(uint32_t *)data;
        // Set scatter-gather parameters
        _this->scatter_length_reg = value;
    }

    if (offset == 12)
    {
        uint32_t value = *(uint32_t *)data;
        // Start transaction
        // value[31:16] is the transaction type, value[15:0] is the transaction level
        int txn_type = (value >> 16) & 0xFF;
        int txn_level = value & 0xFFFF;

        // TODO: handle transaction type and level
        if (txn_type == UnifiedInterconnect::TXN_TYPE_REDUCE)
        {
            _this->process_reduce(_this->addr_reg, _this->size_reg, txn_level);
        } else if (txn_type == UnifiedInterconnect::TXN_TYPE_SCATTER)
        {
            _this->process_scatter(_this->addr_reg, _this->size_reg, _this->scatter_length_reg);
        } else {
            _this->trace.fatal("[ClusterCSR] Error: Unsupported transaction type %d\n", txn_type);
        }

        // Trigger Send FSM to process the transaction
        _this->event_enqueue(_this->send_fsm_event, 1);
    }

    if(offset == 16){
        uint32_t value = *(uint32_t *)data;
        char c = (char)value;
        if (c == '\n') {
            std::cout << _this->buffer << std::endl;
            _this->buffer.clear();
        } else {
            _this->buffer += c;
        }
    }

    if(offset == 20){
        uint32_t activity_type = *(uint32_t *)data;
        if (activity_type >= ACTIVITY_NUM)
        {
            _this->trace.fatal("[ClusterCSR] Error: Invalid activity type %d\n", activity_type);
        }
        if (is_write) {
            _this->activity_timestamps[activity_type] = _this->time.get_time()/1000;
        } else {
            _this->trace.msg("%s: %d ns -> %d ns | period = %d ns \n",
            _this->activity_names[activity_type].c_str(),
            _this->activity_timestamps[activity_type],
            _this->time.get_time()/1000,
            _this->time.get_time()/1000 - _this->activity_timestamps[activity_type]);
        }
    }

    return vp::IO_REQ_OK;
}

vp::IoReqStatus ClusterCSR::port_req(vp::Block *__this, vp::IoReq *req)
{
    ClusterCSR *_this = (ClusterCSR *)__this;
    
    int is_last = *(int *)req->arg_get(UnifiedInterconnect::REQ_IS_LAST);
    if(is_last == 1)
    {
        _this->lock_granted = 1;
        if (_this->lock_required)
        {
            _this->lock_granted = 0;
            _this->lock_required = 0;
            _this->lock_ack_itf.sync(1);
        }
    }

    return vp::IO_REQ_OK;
}

void ClusterCSR::send_fsm_handler(vp::Block *__this, vp::ClockEvent *event)
{
    ClusterCSR *_this = (ClusterCSR *)__this;

    if (_this->send_queue.size() > 0)
    {
        // 1. Check if the head of the queue is ready to be sent
        req_buf_t head = _this->send_queue.front();
        if (head.timestamp <= _this->clock.get_cycles() && head.is_stall == false)
        {
            // 2. Send the request
            vp::IoReq *req = head.req;
            vp::IoReqStatus status = _this->port_out.req(req);
            if (status == vp::IO_REQ_DENIED)
            {
                _this->send_queue.front().is_stall = true;
                _this->trace.msg(vp::Trace::LEVEL_TRACE, "[ClusterCSR] Output port is stalled, wait for grant\n");
            }

            if (status == vp::IO_REQ_OK){
                _this->del_req(req);
            }

            if (status == vp::IO_REQ_INVALID)
            {
                _this->trace.fatal("[ClusterCSR] Error: Invalid request sent to output port\n");
            }

            _this->send_queue.pop();
        }
    }

    if (_this->send_queue.size() > 0)
    {
        _this->event_enqueue(_this->send_fsm_event, 1);
    }
    
}

// This gets called after a request sent to a target was denied, and it is now granted
void ClusterCSR::grant(vp::Block *__this, vp::IoReq *req)
{
    ClusterCSR *_this = (ClusterCSR *)__this;

    req_buf_t head = _this->send_queue.front();
    if (head.is_stall == true)
    {
        _this->send_queue.pop();
        _this->trace.msg(vp::Trace::LEVEL_TRACE, "[ClusterCSR] Output port is un-stalled\n");
        _this->event_enqueue(_this->send_fsm_event, 1);
    } else {
        _this->trace.fatal("[ClusterCSR] Error: Received grant for a request that is not stalled\n");
    }

}

void ClusterCSR::response(vp::Block *__this, vp::IoReq *req)
{
    ClusterCSR *_this = (ClusterCSR *)__this;
    _this->trace.msg(vp::Trace::LEVEL_TRACE, "[ClusterCSR CallBack] Receive response from output interface and delete req %p\n", req);
    _this->del_req(req);
}

void ClusterCSR::lock_sync(vp::Block *__this, bool value)
{
    ClusterCSR *_this = (ClusterCSR *)__this;

    if (_this->lock_granted)
    {
        _this->lock_granted = 0;
        _this->lock_required = 0;
        _this->lock_ack_itf.sync(1);
    } else {
        _this->lock_required = 1;
    }
}

void ClusterCSR::reset(bool active)
{
    this->lock_required = 0;
    this->lock_granted = 0;
}

vp::IoReq * ClusterCSR::new_req(vp::IoReq *req)
{
    vp::IoReq * tmp_req = new vp::IoReq();
    tmp_req->init();
    tmp_req->arg_alloc(UnifiedInterconnect::REQ_NB_ARGS);
    for (int i = 0; i < UnifiedInterconnect::REQ_NB_ARGS; ++i)
    {
        *tmp_req->arg_get(i) = *req->arg_get(i);
    }
    tmp_req->set_addr(req->get_addr());
    tmp_req->set_size(req->get_size());
    tmp_req->set_is_write(req->get_is_write());
    uint8_t * data = new uint8_t[req->get_size()];
    memcpy(data, req->get_data(), req->get_size());
    tmp_req->set_data(data);
    return tmp_req;
}

vp::IoReq * ClusterCSR::del_req(vp::IoReq *req)
{
    uint8_t * data = (uint8_t *)req->get_data();
    delete[] data;
    delete req;
    return NULL;
}


extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
    return new ClusterCSR(config);
}


