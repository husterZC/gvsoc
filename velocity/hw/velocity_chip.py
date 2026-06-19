#
# Copyright (C) 2020 ETH Zurich and University of Bologna
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

# Author: Chi Zhang <chizhang@ethz.ch>

import memory.memory
import interco.router as router
from vp.clock_domain import Clock_domain
import gvsoc.systree
from pulp.chips.velocity.cluster_unit import ClusterUnit, ClusterArch
from pulp.chips.velocity.unified_interco import create_interconnect
from pulp.chips.velocity.platform_ctrl import PlatformCtrl
from pulp.chips.velocity.velocity_ctrl import VelocityCtrl
from pulp.chips.velocity.velocity_arch import VelocityArch


class InterconnectArchView:
    """Expose one endpoint domain through the legacy unified_interco names."""

    def __init__(
        self,
        arch,
        prefix,
        num_endpoint,
        endpoint_stride,
        bus_width,
        write_buffer_size,
        legacy_fallback=False,
    ):
        self._arch = arch
        self._prefix = prefix
        self._legacy_fallback = legacy_fallback
        self.num_cluster = num_endpoint
        self.dma_cluster_stride = endpoint_stride
        self.dma_bus_width = bus_width
        self.dma_write_buffer_size = write_buffer_size

    def __getattr__(self, name):
        if name == 'unified_interco':
            value = getattr(self._arch, self._prefix, None)
            if value is None and self._legacy_fallback:
                value = getattr(self._arch, name, None)
            return value

        if name == 'unified_interco_topology':
            value = getattr(self._arch, f'{self._prefix}_topology', None)
            if value is None and self._legacy_fallback:
                value = getattr(self._arch, name, None)
            return value

        if name.startswith('unified_interco_'):
            short_name = f'{self._prefix}_{name[len("unified_interco_"):]}'
            if hasattr(self._arch, short_name):
                return getattr(self._arch, short_name)
            if self._legacy_fallback and hasattr(self._arch, name):
                return getattr(self._arch, name)

        return getattr(self._arch, name)


def _binary_from_parser(parser):
    if parser is None:
        return None

    args, _ = parser.parse_known_args()
    return args.binary


def _requested_interconnect(arch, name, legacy_name=None):
    requested = getattr(arch, name, None)
    if requested is None and legacy_name is not None:
        requested = getattr(arch, legacy_name, None)
    return requested


class VelocityChip(gvsoc.systree.Component):

    def __init__(self, parent, name, arch, binary, chip_id=0):
        super().__init__(parent, name)

        ##############
        # Components #
        ##############

        cluster_list = []
        for cluster_id in range(arch.num_cluster):
            cluster_arch = ClusterArch(
                num_cluster=arch.num_cluster,
                cluster_id=cluster_id,
                chip_id=chip_id,
                num_chip=getattr(arch, 'num_chip', 1),
                num_lane=arch.cluster_num_lane,
                lane_width=arch.cluster_lane_width,
                inst_base=arch.instruction_mem_base,
                inst_size=arch.instruction_mem_size,
                tcdm_base=arch.cluster_tcdm_base,
                tcdm_size=arch.cluster_tcdm_size,
                stack_base=arch.cluster_stack_base,
                stack_size=arch.cluster_stack_size,
                zomem_base=arch.cluster_zomem_base,
                zomem_size=arch.cluster_zomem_size,
                reg_base=arch.cluster_reg_base,
                reg_size=arch.cluster_reg_size,
                dma_reg_offset=arch.dma_reg_offset,
                dma_reg_size=arch.dma_reg_size,
                dma_bus_width=arch.dma_bus_width,
                dma_read_buffer_size=arch.dma_read_buffer_size,
                dma_write_buffer_size=arch.dma_write_buffer_size,
                dma_max_inflight_txn=arch.dma_max_inflight_txn,
                dma_base_latency=arch.dma_base_latency,
                dma_cluster_stride=arch.dma_cluster_stride,
                rdma_enabled=cluster_id == 0,
                rdma_reg_offset=getattr(arch, 'rdma_reg_offset', 0),
                rdma_reg_size=getattr(arch, 'rdma_reg_size', 0),
                rdma_bus_width=getattr(arch, 'rdma_bus_width', arch.dma_bus_width),
                rdma_read_buffer_size=getattr(arch, 'rdma_read_buffer_size', arch.dma_read_buffer_size),
                rdma_write_buffer_size=getattr(arch, 'rdma_write_buffer_size', arch.dma_write_buffer_size),
                rdma_max_inflight_txn=getattr(arch, 'rdma_max_inflight_txn', arch.dma_max_inflight_txn),
                rdma_base_latency=getattr(arch, 'rdma_base_latency', arch.dma_base_latency),
                rdma_chip_stride=getattr(arch, 'rdma_chip_stride', arch.dma_cluster_stride),
            )
            cluster_list.append(ClusterUnit(self, f'cluster_{cluster_id}', cluster_arch, binary))

        virtual_interco = router.Router(self, 'virtual_interco', bandwidth=8)

        onchip_arch = InterconnectArchView(
            arch,
            'onchip',
            arch.num_cluster,
            arch.dma_cluster_stride,
            arch.dma_bus_width,
            arch.dma_write_buffer_size,
            legacy_fallback=True,
        )
        onchip_requested = _requested_interconnect(arch, 'onchip', 'unified_interco')
        if onchip_requested:
            onchip_interco = create_interconnect(self, 'onchip_interco', cluster_list, onchip_arch)
        else:
            onchip_interco = router.Router(
                self,
                'onchip_interco',
                bandwidth=arch.dma_bus_width,
                synchronous=False,
                max_input_pending_size=arch.dma_write_buffer_size,
            )

        debug_mem = memory.memory.Memory(self, 'debug_mem', size=1)
        velocity_ctrl = VelocityCtrl(
            self,
            'velocity_ctrl',
            num_cluster=arch.num_cluster,
            chip_id=chip_id,
            num_chip=getattr(arch, 'num_chip', 1),
        )

        ############
        # Bindings #
        ############

        virtual_interco.o_MAP(debug_mem.i_INPUT())
        virtual_interco.o_MAP(
            velocity_ctrl.i_INPUT(),
            base=arch.soc_register_base,
            size=arch.soc_register_size,
            rm_base=True,
        )

        for cluster_id in range(arch.num_cluster):
            cluster = cluster_list[cluster_id]
            cluster.o_VIRTUAL_SOC(virtual_interco.i_INPUT())
            if onchip_requested:
                cluster.o_DMA_REMOTE(onchip_interco.i_CLUSTER_INPUT(cluster_id))
                onchip_interco.o_CLUSTER_OUTPUT(cluster_id, cluster.i_DMA_REMOTE())
            else:
                cluster.o_DMA_REMOTE(onchip_interco.i_INPUT())
                onchip_interco.o_MAP(
                    cluster.i_DMA_REMOTE(),
                    base=cluster_id * arch.dma_cluster_stride,
                    size=arch.dma_cluster_stride,
                    rm_base=True,
                )

        if cluster_list:
            self.bind(cluster_list[0], 'rdma_remote_out', self, 'rdma_remote_out')
            self.bind(self, 'rdma_remote_in', cluster_list[0], 'rdma_remote_in')

        velocity_ctrl.o_EOC(gvsoc.systree.SlaveItf(
            self, 'eoc', signature='wire<uint32_t>'))

    def i_RDMA_REMOTE(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'rdma_remote_in', signature='io')

    def o_RDMA_REMOTE(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('rdma_remote_out', itf, signature='io')

    def o_EOC(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('eoc', itf, signature='wire<uint32_t>')


class VelocityPlatform(gvsoc.systree.Component):

    def __init__(self, parent, name, parser, options):
        super(VelocityPlatform, self).__init__(parent, name, options=options)

        arch = VelocityArch()
        binary = _binary_from_parser(parser)
        frequency = getattr(arch, 'frequency', getattr(arch, 'frequence', 1000000000))
        num_chip = getattr(arch, 'num_chip', 1)

        clock = Clock_domain(self, 'clock', frequency=frequency)
        chips = [
            VelocityChip(self, f'chip_{chip_id}', arch, binary, chip_id=chip_id)
            for chip_id in range(num_chip)
        ]
        platform_ctrl = PlatformCtrl(self, 'platform_ctrl', num_chip=num_chip)

        for chip in chips:
            self.bind(clock, 'out', chip, 'clock')
        self.bind(clock, 'out', platform_ctrl, 'clock')

        for chip_id, chip in enumerate(chips):
            chip.o_EOC(platform_ctrl.i_CHIP_EOC(chip_id))

        if not chips:
            return

        if num_chip == 1:
            offchip_interco = router.Router(
                self,
                'offchip_interco',
                bandwidth=getattr(arch, 'rdma_bus_width', arch.dma_bus_width),
                synchronous=False,
                max_input_pending_size=getattr(arch, 'rdma_write_buffer_size', arch.dma_write_buffer_size),
            )
            chips[0].o_RDMA_REMOTE(offchip_interco.i_INPUT())
            offchip_interco.o_MAP(
                chips[0].i_RDMA_REMOTE(),
                base=0,
                size=getattr(arch, 'rdma_chip_stride', arch.dma_cluster_stride),
                rm_base=True,
            )
            self.bind(clock, 'out', offchip_interco, 'clock')
            return

        offchip_arch = InterconnectArchView(
            arch,
            'offchip',
            num_chip,
            getattr(arch, 'rdma_chip_stride', arch.dma_cluster_stride),
            getattr(arch, 'rdma_bus_width', arch.dma_bus_width),
            getattr(arch, 'rdma_write_buffer_size', arch.dma_write_buffer_size),
        )
        offchip_requested = getattr(arch, 'offchip', None)
        if offchip_requested:
            offchip_interco = create_interconnect(self, 'offchip_interco', chips, offchip_arch)
            for chip_id, chip in enumerate(chips):
                chip.o_RDMA_REMOTE(offchip_interco.i_CLUSTER_INPUT(chip_id))
                offchip_interco.o_CLUSTER_OUTPUT(chip_id, chip.i_RDMA_REMOTE())
        else:
            offchip_interco = router.Router(
                self,
                'offchip_interco',
                bandwidth=getattr(arch, 'rdma_bus_width', arch.dma_bus_width),
                synchronous=False,
                max_input_pending_size=getattr(arch, 'rdma_write_buffer_size', arch.dma_write_buffer_size),
            )
            for chip_id, chip in enumerate(chips):
                chip.o_RDMA_REMOTE(offchip_interco.i_INPUT())
                offchip_interco.o_MAP(
                    chip.i_RDMA_REMOTE(),
                    base=chip_id * getattr(arch, 'rdma_chip_stride', arch.dma_cluster_stride),
                    size=getattr(arch, 'rdma_chip_stride', arch.dma_cluster_stride),
                    rm_base=True,
                )

        self.bind(clock, 'out', offchip_interco, 'clock')
