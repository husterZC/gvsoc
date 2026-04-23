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

/*
 * Authors: Chi Zhang, ETH Zurich (chizhang@iis.ee.ethz.ch)
 */

#include <vp/vp.hpp>
#include <vp/itf/io.hpp>
#include <iostream>
#include <vector>
#include <queue>
#include <list>
#include <algorithm>
#include "../unified_interconnect.hpp"

class Tree : public vp::Component
{

public:
    Tree(vp::ComponentConf &config);

private:
    void                                            reset(bool active);
    static vp::IoReqStatus                          req(vp::Block *__this, vp::IoReq *req);
    static std::vector<int>                         string2vector(std::string str);
    static void                                     collective_fsm_handler(vp::Block *__this, vp::ClockEvent *event);
    std::vector<int>                                id2coor(int id);
    vp::IoReq *                                     new_req(vp::IoReq *req, int port);
    vp::IoReq *                                     del_req(vp::IoReq *req);
    vp::Trace                                       trace;
    vp::IoSlave                                     input_itf;
    vp::IoMaster                                    output_itf;
    int                                             at_level;
    int                                             num_level;
    int                                             num_sub;
    std::vector<int>                                levels;
    std::vector<int>                                coordinates;
    vp::ClockEvent *                                collective_fsm_event;
    std::vector<std::pair<int64_t, vp::IoReq *>>    collective_pipeline;
    int                                             collective_queue_depth;
    int                                             collective_reduce_latency;
    vp::IoReq *                                     reduce_req;
    std::vector<bool>                               reduce_port_record;
};

Tree::Tree(vp::ComponentConf &config)
    : vp::Component(config)
{
    this->traces.new_trace("trace", &this->trace, vp::DEBUG);
    this->input_itf.set_req_meth(&Tree::req);
    this->new_slave_port("in", &this->input_itf);
    this->new_master_port("out", &this->output_itf);
    this->at_level = this->get_js_config()->get("at_level")->get_int();
    this->num_level = this->get_js_config()->get("num_level")->get_int();
    this->num_sub = this->get_js_config()->get("num_sub")->get_int();
    this->levels = string2vector(this->get_js_config()->get("levels")->get_str());
    this->coordinates = string2vector(this->get_js_config()->get("coordinates")->get_str());
    this->collective_fsm_event = this->event_new(&Tree::collective_fsm_handler);
    this->collective_queue_depth = this->get_js_config()->get("collective_queue_depth")->get_int();
    this->collective_reduce_latency = this->get_js_config()->get("collective_reduce_latency")->get_int();
    this->reduce_req = NULL;
    this->reduce_port_record.resize(this->num_sub);
    for (int i = 0; i < this->num_sub; i++)
    {
        this->reduce_port_record[i] = false;
    }
    
}

void Tree::reset(bool active)
{
    if (active)
    {
        this->trace.msg(vp::Trace::LEVEL_TRACE, "Reset done\n");
    }
}

std::vector<int> Tree::string2vector(std::string str)
{
    std::vector<int> result;
    std::stringstream ss(str);
    std::string token;

    while (std::getline(ss, token, ',')) {
        result.push_back(std::stoi(token));
    }

    return result;
}

std::vector<int> Tree::id2coor(int id)
{
    std::vector<int> coor(this->num_level);
    for (int i = this->num_level - 1; i >= 0; --i)
    {
        int level_sub = this->levels[i];
        coor[i] = id % level_sub;
        id /= level_sub;
    }
    return coor;
}

vp::IoReqStatus Tree::req(vp::Block *__this, vp::IoReq *req)
{
    Tree *_this = (Tree *)__this;

    int REQ_TXN_TYPE           = *(int *)req->arg_get(UnifiedInterconnect::REQ_TXN_TYPE);
    int REQ_SOUR_ID            = *(int *)req->arg_get(UnifiedInterconnect::REQ_SOUR_ID);
    int REQ_DEST_ID            = *(int *)req->arg_get(UnifiedInterconnect::REQ_DEST_ID);
    int REQ_IS_LAST            = *(int *)req->arg_get(UnifiedInterconnect::REQ_IS_LAST);
    int REQ_SIZE               = *(int *)req->arg_get(UnifiedInterconnect::REQ_SIZE);
    int REQ_ACK                = *(int *)req->arg_get(UnifiedInterconnect::REQ_ACK);
    int REQ_PORT_ID            = *(int *)req->arg_get(UnifiedInterconnect::REQ_PORT_ID);
    int REQ_COLL_PHASE         = *(int *)req->arg_get(UnifiedInterconnect::REQ_COLL_PHASE);
    int REQ_COLL_LEVEL         = *(int *)req->arg_get(UnifiedInterconnect::REQ_COLL_LEVEL);
    int REQ_COLL_SCATTER_SIZE  = *(int *)req->arg_get(UnifiedInterconnect::REQ_COLL_SCATTER_SIZE);
    int REQ_COLL_SCATTER_START = *(int *)req->arg_get(UnifiedInterconnect::REQ_COLL_SCATTER_START);
    int REQ_COLL_SCATTER_END   = *(int *)req->arg_get(UnifiedInterconnect::REQ_COLL_SCATTER_END);

    if (REQ_TXN_TYPE != UnifiedInterconnect::TXN_TYPE_REDUCE && \
        REQ_TXN_TYPE != UnifiedInterconnect::TXN_TYPE_ALLREDUCE && \
        REQ_TXN_TYPE != UnifiedInterconnect::TXN_TYPE_SCATTER) {
        _this->trace.fatal("[Tree] Error: Unsupported transaction type %d\n", REQ_TXN_TYPE);
        return vp::IO_REQ_INVALID;
    }
    *req->arg_get(UnifiedInterconnect::REQ_ACK) = (void *)0;

    if (_this->collective_pipeline.size() > _this->collective_queue_depth)
    {
        *req->arg_get(UnifiedInterconnect::REQ_ACK) = (void *)UnifiedInterconnect::ACK_COLLECTIVE_REJECT;
        return vp::IO_REQ_OK;
    }
    
    if (REQ_TXN_TYPE == UnifiedInterconnect::TXN_TYPE_REDUCE)
    {
        _this->trace.msg(vp::Trace::LEVEL_TRACE, "[Tree] Deal with Reduce\n");
        if (REQ_PORT_ID == 0)
        {
            _this->trace.fatal("[Tree] Error: Reduction can not recieve top to down request %d\n", REQ_PORT_ID);
            return vp::IO_REQ_INVALID;
        }

        int sub_id = REQ_PORT_ID - 1;

        if (_this->reduce_port_record[sub_id] == true)
        {
            *req->arg_get(UnifiedInterconnect::REQ_ACK) = (void *)UnifiedInterconnect::ACK_COLLECTIVE_REJECT;
            return vp::IO_REQ_OK;
        }

        *req->arg_get(UnifiedInterconnect::REQ_ACK) = (void *)UnifiedInterconnect::ACK_COLLECTIVE_ACCEPT;

        _this->reduce_port_record[sub_id] = true;
        if (_this->reduce_req == NULL)
        {
            _this->reduce_req = _this->new_req(req, 0);
            *(_this->reduce_req->arg_get(UnifiedInterconnect::REQ_ACK)) = (void *)0;
        }

        //TODO: Do reduction

        //Check reduction done
        bool reduce_done = true;
        for (int i = 0; i < _this->num_sub; i++)
        {
            reduce_done = reduce_done & _this->reduce_port_record[i];
        }

        if (reduce_done)
        {
            //Push to collective pipeline
            std::pair<int64_t, vp::IoReq *> pair;
            pair.first = _this->clock.get_cycles() + _this->collective_reduce_latency;
            pair.second = _this->reduce_req;
            _this->collective_pipeline.push_back(pair);
            _this->event_enqueue(_this->collective_fsm_event, 1);

            //Reset
            _this->reduce_req = NULL;
            for (int i = 0; i < _this->num_sub; i++)
            {
                _this->reduce_port_record[i] = false;
            }
        }
    }

    if (REQ_TXN_TYPE == UnifiedInterconnect::TXN_TYPE_SCATTER)
    {
        _this->trace.msg(vp::Trace::LEVEL_TRACE, "[Tree] Deal with Scatter\n");
        if (REQ_PORT_ID != 0)
        {
            _this->trace.fatal("[Tree] Error: Scatter can not recieve down to top request %d\n", REQ_PORT_ID);
            return vp::IO_REQ_INVALID;
        }

        if (REQ_COLL_SCATTER_SIZE > REQ_SIZE)
        {
            _this->trace.fatal("[Tree] Error: Scatter elements size %d can bot larger than flit granularity %d\n", REQ_COLL_SCATTER_SIZE, REQ_SIZE);
            return vp::IO_REQ_INVALID;
        }

        if((REQ_COLL_SCATTER_END - REQ_COLL_SCATTER_START + 1) > (REQ_SIZE / REQ_COLL_SCATTER_SIZE))
        {
            _this->trace.fatal("[Tree] Error: Scatter from %d to %d is larger than number of emelemnt of a flit %d\n", REQ_COLL_SCATTER_START, REQ_COLL_SCATTER_END, REQ_SIZE / REQ_COLL_SCATTER_SIZE);
            return vp::IO_REQ_INVALID;
        }

        //Check start and end id range
        std::vector<int> start_coor = _this->id2coor(REQ_COLL_SCATTER_START);
        std::vector<int> end_coor = _this->id2coor(REQ_COLL_SCATTER_START);
        for (int l = 0; l < _this->at_level; l++)
        {
            if(start_coor[l] != _this->coordinates[l]) {_this->trace.fatal("[Tree] Error: Scatter from level %d: start coor %d not match the node %d\n", l, start_coor[l], _this->coordinates[l]); return vp::IO_REQ_INVALID;}
            if(end_coor[l] != _this->coordinates[l]) {_this->trace.fatal("[Tree] Error: End from level %d: end coor %d not match the node %d\n", l, start_coor[l], _this->coordinates[l]); return vp::IO_REQ_INVALID;}
        }

        //Iterate over all elements
        *req->arg_get(UnifiedInterconnect::REQ_ACK) = (void *)UnifiedInterconnect::ACK_COLLECTIVE_ACCEPT;
        std::vector<int> scatter_dest_list;
        int current_sub_id = start_coor[_this->at_level];
        scatter_dest_list.push_back(current_sub_id);
        for (int i = REQ_COLL_SCATTER_START + 1; i <= REQ_COLL_SCATTER_END; i++)
        {
            std::vector<int> coor = _this->id2coor(i);
            if (coor[_this->at_level] != current_sub_id)
            {
                current_sub_id = coor[_this->at_level];
                scatter_dest_list.push_back(current_sub_id);
            }
        }
        //TODO: Scatter the data
        _this->trace.msg(vp::Trace::LEVEL_TRACE, "[Tree] Scatter to %d subtrees\n", scatter_dest_list.size());
        for (int dest_sub : scatter_dest_list) {
            //Push to collective pipeline
            std::pair<int64_t, vp::IoReq *> pair;
            int dest_port = dest_sub + 1;
            pair.first = _this->clock.get_cycles() + 1;
            vp::IoReq * tmp_req = _this->new_req(req, dest_port);
            if (_this->at_level == _this->num_level - 1)
            {
                *tmp_req->arg_get(UnifiedInterconnect::REQ_IS_LAST) = (void *)1;
            }
            pair.second = tmp_req;
            _this->collective_pipeline.push_back(pair);
            _this->event_enqueue(_this->collective_fsm_event, 1);
        }
    }
    return vp::IO_REQ_OK;
}

void Tree::collective_fsm_handler(vp::Block *__this, vp::ClockEvent *event)
{
    Tree *_this = (Tree *)__this;

    //Iterate over the pipeline
    std::vector<std::pair<int64_t, vp::IoReq *>> remain;
    for (std::pair<int64_t, vp::IoReq *> pair: _this->collective_pipeline)
    {
        if (_this->clock.get_cycles() >= pair.first)
        {
            vp::IoReq * tmp_req = pair.second;
            *tmp_req->arg_get(UnifiedInterconnect::REQ_ACK) = (void *)0;
            vp::IoReqStatus result = _this->output_itf.req(tmp_req);
            if (result != vp::IO_REQ_OK)
            {
                _this->trace.fatal("[Tree] Error: No valid router response\n");
            }
            int ack = *(int *)tmp_req->arg_get(UnifiedInterconnect::REQ_ACK);
            if (ack == UnifiedInterconnect::ACK_COLLECTIVE_ACCEPT)
            {
                _this->del_req(tmp_req);
                continue;
            }
        }
        remain.push_back(pair);
    }
    _this->collective_pipeline = remain;

    if (_this->collective_pipeline.size() > 0)
    {
        _this->event_enqueue(_this->collective_fsm_event, 1);
    }
}

vp::IoReq * Tree::new_req(vp::IoReq *req, int port_id)
{
    vp::IoReq * tmp_req = new vp::IoReq();
    tmp_req->init();
    tmp_req->arg_alloc(UnifiedInterconnect::REQ_NB_ARGS);
    for (int i = 0; i < UnifiedInterconnect::REQ_NB_ARGS; ++i)
    {
        *tmp_req->arg_get(i) = *req->arg_get(i);
    }
    *tmp_req->arg_get(UnifiedInterconnect::REQ_PORT_ID) = (void *)port_id;
    tmp_req->set_addr(req->get_addr());
    tmp_req->set_size(req->get_size());
    tmp_req->set_is_write(req->get_is_write());
    uint8_t * data = new uint8_t[req->get_size()];
    memcpy(data, req->get_data(), req->get_size());
    tmp_req->set_data(data);
    return tmp_req;
}

vp::IoReq * Tree::del_req(vp::IoReq *req)
{
    uint8_t * data = (uint8_t *)req->get_data();
    delete[] data;
    delete req;
    return NULL;
}

extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
    return new Tree(config);
}

