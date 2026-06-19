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

#include <vp/vp.hpp>
#include <vp/itf/wire.hpp>
#include <cstdint>
#include <string>
#include <vector>


class PlatformCtrl : public vp::Component
{

public:
    PlatformCtrl(vp::ComponentConf &config);

private:
    static void chip_eoc_sync(vp::Block *__this, uint32_t status, int chip_id);
    void reset(bool active);

    vp::Trace trace;
    uint32_t num_chip;
    uint32_t done_count;
    uint32_t exit_status;
    std::vector<bool> chip_done;
    std::vector<vp::WireSlave<uint32_t>> chip_eoc_itfs;
};


PlatformCtrl::PlatformCtrl(vp::ComponentConf &config)
    : vp::Component(config)
{
    this->num_chip = this->get_js_config()->get("num_chip")->get_int();

    this->traces.new_trace("trace", &this->trace, vp::DEBUG);

    this->done_count = 0;
    this->exit_status = 0;
    this->chip_done.resize(this->num_chip);
    this->chip_eoc_itfs.resize(this->num_chip);

    for (uint32_t chip_id = 0; chip_id < this->num_chip; chip_id++)
    {
        this->chip_eoc_itfs[chip_id].set_sync_meth_muxed(
            &PlatformCtrl::chip_eoc_sync, chip_id);
        this->new_slave_port(
            "chip_eoc_" + std::to_string(chip_id), &this->chip_eoc_itfs[chip_id]);
    }
}


void PlatformCtrl::reset(bool active)
{
    if (active)
    {
        this->done_count = 0;
        this->exit_status = 0;
        for (uint32_t chip_id = 0; chip_id < this->num_chip; chip_id++)
        {
            this->chip_done[chip_id] = false;
        }
    }
}


void PlatformCtrl::chip_eoc_sync(vp::Block *__this, uint32_t status, int chip_id)
{
    PlatformCtrl *_this = (PlatformCtrl *)__this;

    if (chip_id < 0 || (uint32_t)chip_id >= _this->num_chip)
    {
        return;
    }

    if (_this->chip_done[chip_id])
    {
        _this->trace.msg("Ignoring duplicate EOC from chip %d with status %d\n",
            chip_id, status);
        return;
    }

    _this->chip_done[chip_id] = true;
    _this->done_count += 1;
    _this->exit_status |= status;
    _this->trace.msg("Chip %d EOC status %d, done %d/%d\n",
        chip_id, status, _this->done_count, _this->num_chip);

    if (_this->done_count >= _this->num_chip)
    {
        _this->time.get_engine()->quit(_this->exit_status);
    }
}


extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
    return new PlatformCtrl(config);
}
