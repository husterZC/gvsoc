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
 * Authors: Germain Haugou, ETH Zurich (germain.haugou@iis.ee.ethz.ch)
            Yichao  Zhang , ETH Zurich (yiczhang@iis.ee.ethz.ch)
            Chi     Zhang , ETH Zurich (chizhang@iis.ee.ethz.ch)
 */

#include <vp/vp.hpp>
#include <vp/itf/io.hpp>
#include <vp/itf/wire.hpp>
#include <cstdint>
#include <iostream>
#include <vector>

namespace {

constexpr uint64_t REG_EOC             = 0x00;
constexpr uint64_t REG_EOC_ALL         = 0x04;
constexpr uint64_t REG_TIMER_START     = 0x08;
constexpr uint64_t REG_TIMER_END_PRINT = 0x0c;
constexpr uint64_t REG_LOG_CHAR        = 0x10;
constexpr uint64_t REG_LOG_INT         = 0x14;
constexpr uint64_t REG_TIME_LO         = 0x18;
constexpr uint64_t REG_TIME_HI         = 0x1c;
constexpr uint64_t REG_TIMER_LO        = 0x20;
constexpr uint64_t REG_TIMER_HI        = 0x24;
constexpr uint64_t REG_BARRIER_ARRIVE  = 0x28;
constexpr uint64_t REG_BARRIER_PHASE   = 0x2c;
constexpr uint64_t REG_BARRIER_COUNT   = 0x30;

} // namespace

void printProgressBar(int progress, int total, int width = 100) {
    float ratio = (float)progress / total;
    int filled = (int)(ratio * width);

    std::cout << "\r[";
    for (int i = 0; i < width; ++i) {
        if (i < filled)
            std::cout << "=";
        else if (i == filled)
            std::cout << ">";
        else
            std::cout << " ";
    }

    std::cout << "] " << (int)(ratio * 100) << "%\n";
    std::cout.flush();
}


class VelocityCtrl : public vp::Component
{

public:
    VelocityCtrl(vp::ComponentConf &config);

private:
    static vp::IoReqStatus req(vp::Block *__this, vp::IoReq *req);
    static void barrier_release(vp::Block *__this, vp::ClockEvent *event);
    void reset(bool active);

    vp::Trace               trace;
    vp::IoSlave             input_itf;
    uint32_t                num_cluster;
    int64_t                 timer_start;
    int64_t                 all_eoc_conuter;
    uint32_t                barrier_count;
    uint32_t                barrier_phase;
    std::vector<vp::IoReq *> barrier_reqs;
    vp::ClockEvent          *barrier_release_event;
};



VelocityCtrl::VelocityCtrl(vp::ComponentConf &config)
    : vp::Component(config)
{
    this->num_cluster = this->get_js_config()->get("num_cluster")->get_int();

    this->traces.new_trace("trace", &this->trace, vp::DEBUG);
    this->input_itf.set_req_meth(&VelocityCtrl::req);
    this->new_slave_port("input", &this->input_itf);
    this->timer_start = 0;
    this->all_eoc_conuter = 0;
    this->barrier_count = 0;
    this->barrier_phase = 0;
    this->barrier_release_event = this->event_new(VelocityCtrl::barrier_release);
}

void VelocityCtrl::reset(bool active)
{
    if (active)
    {
        std::cout << "[SystemInfo]: num_cluster = " << this->num_cluster << std::endl;
    }
}

vp::IoReqStatus VelocityCtrl::req(vp::Block *__this, vp::IoReq *req)
{
    VelocityCtrl *_this = (VelocityCtrl *)__this;

    uint64_t offset = req->get_addr();
    uint8_t *data = req->get_data();
    uint64_t size = req->get_size();
    bool is_write = req->get_is_write();

    if (size != 4)
    {
        return vp::IO_REQ_INVALID;
    }

    if (is_write)
    {
        uint32_t value = *(uint32_t *)data;
        if (offset == REG_EOC)
        {
            // std::cout << "EOC register return value: 0x" << std::hex << value << std::endl;
            _this->time.get_engine()->quit(0);
        }
        else if (offset == REG_EOC_ALL)
        {
            _this->all_eoc_conuter += 1;
            // _this->trace.msg("Control registers access (offset: 0x%x, size: 0x%x, is_write: %d, data:%x)\n", offset, size, is_write, *(uint32_t *)data);
            printProgressBar(_this->all_eoc_conuter, _this->num_cluster);
            if (_this->all_eoc_conuter >= _this->num_cluster)
            {
                _this->time.get_engine()->quit(0);
            }
        }
        else if (offset == REG_TIMER_START)
        {
            _this->timer_start = _this->time.get_time();
        }
        else if (offset == REG_TIMER_END_PRINT)
        {
            int64_t period = _this->time.get_time() - _this->timer_start;
            std::cout << "[Performance Counter]: Execution period is " << period/1000 << " ns" << std::endl;
            _this->timer_start = _this->time.get_time();
        }
        else if (offset == REG_LOG_CHAR)
        {
            char c = (char)value;
            std::cout << c;
        }
        else if (offset == REG_LOG_INT)
        {
            std::cout << value;
        }
        else if (offset == REG_BARRIER_ARRIVE)
        {
            _this->barrier_count += 1;
            _this->barrier_reqs.push_back(req);
            if (_this->barrier_count >= _this->num_cluster)
            {
                _this->barrier_count = 0;
                _this->barrier_phase += 1;
                _this->barrier_release_event->enqueue(1);
            }
            return vp::IO_REQ_PENDING;
        }
        else
        {
            return vp::IO_REQ_INVALID;
        }

        return vp::IO_REQ_OK;
    }

    uint64_t value = 0;
    switch (offset)
    {
        case REG_TIME_LO:
            value = _this->time.get_time();
            *(uint32_t *)data = value & 0xffffffff;
            break;
        case REG_TIME_HI:
            value = _this->time.get_time();
            *(uint32_t *)data = value >> 32;
            break;
        case REG_TIMER_LO:
            value = _this->time.get_time() - _this->timer_start;
            *(uint32_t *)data = value & 0xffffffff;
            break;
        case REG_TIMER_HI:
            value = _this->time.get_time() - _this->timer_start;
            *(uint32_t *)data = value >> 32;
            break;
        case REG_BARRIER_PHASE:
            *(uint32_t *)data = _this->barrier_phase;
            break;
        case REG_BARRIER_COUNT:
            *(uint32_t *)data = _this->barrier_count;
            break;
        default:
            return vp::IO_REQ_INVALID;
    }

    return vp::IO_REQ_OK;
}

void VelocityCtrl::barrier_release(vp::Block *__this, vp::ClockEvent *event)
{
    VelocityCtrl *_this = (VelocityCtrl *)__this;

    std::vector<vp::IoReq *> pending_reqs;
    pending_reqs.swap(_this->barrier_reqs);

    for (vp::IoReq *pending_req: pending_reqs)
    {
        pending_req->get_resp_port()->resp(pending_req);
    }
}


extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
    return new VelocityCtrl(config);
}
