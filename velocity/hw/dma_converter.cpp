/*
 * Copyright (C) 2026 ETH Zurich and University of Bologna
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
#include <string>
#include <vector>

#include <vp/vp.hpp>
#include <vp/itf/io.hpp>

#include "packet_trace.hpp"

class DmaConverter : public vp::Component
{
public:
    DmaConverter(vp::ComponentConf &config);

    static vp::IoReqStatus req(vp::Block *__this, vp::IoReq *req);

private:
    uint64_t bank_offset(uint64_t offset) const;
    int bank_id(uint64_t offset) const;

    vp::Trace trace;
    vp::IoSlave input_port;
    std::vector<vp::IoMaster> output_ports;

    int nb_banks = 0;
    int stage_bits = 0;
    int interleaving_bits = 0;
    uint64_t bank_mask = 0;
    uint64_t offset_mask = ~0ULL;
    uint64_t bank_width = 0;
};

static int ilog2_floor(int value)
{
    int bits = 0;
    while ((1 << (bits + 1)) <= value)
    {
        bits++;
    }
    return bits;
}

DmaConverter::DmaConverter(vp::ComponentConf &config)
    : vp::Component(config)
{
    this->traces.new_trace("trace", &this->trace, vp::DEBUG);

    this->nb_banks = this->get_js_config()->get_child_int("nb_banks");
    this->stage_bits = this->get_js_config()->get_child_int("stage_bits");
    this->interleaving_bits = this->get_js_config()->get_child_int("interleaving_bits");
    uint64_t configured_offset_mask = this->get_js_config()->get_child_int("offset_mask");

    if (this->stage_bits == 0)
    {
        this->stage_bits = ilog2_floor(this->nb_banks);
    }

    this->bank_mask = (1ULL << this->stage_bits) - 1;
    this->bank_width = 1ULL << this->interleaving_bits;
    if (configured_offset_mask != 0)
    {
        this->offset_mask = configured_offset_mask;
    }

    this->input_port.set_req_meth(&DmaConverter::req);
    this->new_slave_port("input", &this->input_port);

    this->output_ports.resize(this->nb_banks);
    for (int i = 0; i < this->nb_banks; i++)
    {
        this->new_master_port("out_" + std::to_string(i), &this->output_ports[i]);
    }
}

int DmaConverter::bank_id(uint64_t offset) const
{
    return (offset >> this->interleaving_bits) & this->bank_mask;
}

uint64_t DmaConverter::bank_offset(uint64_t offset) const
{
    offset &= this->offset_mask;
    return ((offset >> (this->stage_bits + this->interleaving_bits)) << this->interleaving_bits) +
        (offset & ((1ULL << this->interleaving_bits) - 1));
}

vp::IoReqStatus DmaConverter::req(vp::Block *__this, vp::IoReq *req)
{
    DmaConverter *_this = (DmaConverter *)__this;

    uint64_t offset = req->get_addr();
    uint64_t size = req->get_size();
    uint8_t *data = req->get_data();
    uint8_t *second_data = req->get_second_data();
    uint8_t *memcheck_data = req->get_memcheck_data();
    uint8_t *second_memcheck_data = req->get_second_memcheck_data();
    int packet_id = req->get_initiator();
    unsigned int chunks = 0;
    uint64_t max_latency = 0;

    _this->trace.msg(vp::Trace::LEVEL_TRACE,
        "[%9d][TCDM]: dma_converter_begin addr=0x%llx size=%llu is_write=%d\n",
        packet_id, (unsigned long long)offset, (unsigned long long)size, req->get_is_write());

    while (size > 0)
    {
        uint64_t chunk_size = _this->bank_width - (offset & (_this->bank_width - 1));
        chunk_size = std::min(chunk_size, size);

        int bank = _this->bank_id(offset);
        if (bank < 0 || bank >= _this->nb_banks)
        {
            _this->trace.msg(vp::Trace::LEVEL_TRACE,
                "[%9d][TCDM]: dma_converter_invalid_bank bank=%d addr=0x%llx size=%llu\n",
                packet_id, bank, (unsigned long long)offset, (unsigned long long)chunk_size);
            return vp::IO_REQ_INVALID;
        }

        vp::IoReq bank_req;
        bank_req.init();
        bank_req.set_addr(_this->bank_offset(offset));
        bank_req.set_size(chunk_size);
        bank_req.set_data(data);
        bank_req.set_second_data(second_data);
        bank_req.set_memcheck_data(memcheck_data);
        bank_req.set_second_memcheck_data(second_memcheck_data);
        bank_req.set_is_write(req->get_is_write());
        bank_req.set_opcode(req->get_opcode());
        bank_req.set_debug(req->is_debug());
        bank_req.set_initiator(packet_id);

        // The converter is strictly synchronous; bank_req must not outlive this call.
        vp::IoReqStatus status = _this->output_ports[bank].req_forward(&bank_req);
        if (status != vp::IO_REQ_OK)
        {
            _this->trace.msg(vp::Trace::LEVEL_TRACE,
                "[%9d][TCDM]: dma_converter_bank_error bank=%d bank_addr=0x%llx size=%llu status=%s\n",
                packet_id, bank, (unsigned long long)bank_req.get_addr(),
                (unsigned long long)chunk_size, velocity::io_status_name(status));
            return status == vp::IO_REQ_INVALID ? status : vp::IO_REQ_INVALID;
        }

        max_latency = std::max(max_latency, bank_req.get_full_latency());

        offset += chunk_size;
        size -= chunk_size;
        if (data)
        {
            data += chunk_size;
        }
        if (second_data)
        {
            second_data += chunk_size;
        }
        if (memcheck_data)
        {
            memcheck_data += chunk_size;
        }
        if (second_memcheck_data)
        {
            second_memcheck_data += chunk_size;
        }
        chunks++;
    }

    req->inc_latency(max_latency);
    _this->trace.msg(vp::Trace::LEVEL_TRACE,
        "[%9d][TCDM]: dma_converter_end chunks=%u latency=%llu\n",
        packet_id, chunks, (unsigned long long)max_latency);

    return vp::IO_REQ_OK;
}

extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
    return new DmaConverter(config);
}
