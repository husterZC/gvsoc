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

import gvsoc.runner
import cpu.iss.riscv as iss
import memory.memory
import interco.router as router
from vp.clock_domain import Clock_domain
import interco.router as router
import utils.loader.loader
import gvsoc.systree
from pulp.chips.velocity.cluster_unit import ClusterUnit, ClusterArch
from pulp.chips.velocity.velocity_ctrl import VelocityCtrl
from pulp.chips.velocity.velocity_arch import VelocityArch
import math

class VelocitySystem(gvsoc.systree.Component):

    def __init__(self, parent, name, parser):
        super().__init__(parent, name)

        #################
        # Configuration #
        #################

        arch = VelocityArch()

        # Get Binary
        binary = None
        if parser is not None:
            [args, otherArgs] = parser.parse_known_args()
            binary = args.binary


        ##############
        # Components #
        ##############

        #Clusters
        cluster_list=[]
        for cluster_id in range(arch.num_cluster):
            cluster_arch = ClusterArch( num_cluster 		= arch.num_cluster,
            							cluster_id 			= cluster_id,
        								num_lane 			= arch.cluster_num_lane,
        								lane_width 			= arch.cluster_lane_width,
        								inst_base 			= arch.instruction_mem_base,
        								inst_size 			= arch.instruction_mem_size,
        								tcdm_base 			= arch.cluster_tcdm_base,
        								tcdm_size 			= arch.cluster_tcdm_size,
        								stack_base 			= arch.cluster_stack_base,
        								stack_size 			= arch.cluster_stack_size,
        								zomem_base 			= arch.cluster_zomem_base,
        								zomem_size 			= arch.cluster_zomem_size,
        								reg_base 			= arch.cluster_reg_base,
        								reg_size 			= arch.cluster_reg_size,
                                        dma_reg_offset      = arch.dma_reg_offset,
                                        dma_reg_size        = arch.dma_reg_size,
                                        dma_bus_width       = arch.dma_bus_width,
                                        dma_read_buffer_size = arch.dma_read_buffer_size,
                                        dma_write_buffer_size = arch.dma_write_buffer_size,
                                        dma_max_inflight_txn = arch.dma_max_inflight_txn,
                                        dma_base_latency    = arch.dma_base_latency,
                                        dma_cluster_stride  = arch.dma_cluster_stride)
            cluster_list.append(ClusterUnit(self,f'cluster_{cluster_id}', cluster_arch, binary))
            pass

        #Virtual router, just for debugging and non-performance-critical jobs
        virtual_interco = router.Router(self, 'virtual_interco', bandwidth=8)

        # DMA data router. DMA remote requests address clusters by cluster id.
        dma_interco = router.Router(
            self,
            'dma_interco',
            bandwidth=arch.dma_bus_width,
            synchronous=False,
            max_input_pending_size=arch.dma_write_buffer_size,
        )

        #Debug Memory
        debug_mem = memory.memory.Memory(self,'debug_mem', size=1)

        #Control register
        velocity_ctrl = VelocityCtrl(self, 'velocity_ctrl', num_cluster=arch.num_cluster)

        ############
        # Bindings #
        ############

        #Debug memory
        virtual_interco.o_MAP(debug_mem.i_INPUT())

        #Control register
        virtual_interco.o_MAP(velocity_ctrl.i_INPUT(), base=arch.soc_register_base, size=arch.soc_register_size, rm_base=True)

        #Clusters
        for cluster_id in range(arch.num_cluster):
            cluster_list[cluster_id].o_VIRTUAL_SOC(virtual_interco.i_INPUT())
            cluster_list[cluster_id].o_DMA_REMOTE(dma_interco.i_INPUT())
            dma_interco.o_MAP(cluster_list[cluster_id].i_DMA_REMOTE(),
                              base=cluster_id * arch.dma_cluster_stride,
                              size=arch.dma_cluster_stride,
                              rm_base=True)
            pass


class VelocityPlatform(gvsoc.systree.Component):

    def __init__(self, parent, name, parser, options):
        super(VelocityPlatform, self).__init__(parent, name, options=options)

        arch  = VelocityArch()
        clock = Clock_domain(self, 'clock', frequency=(1000000000 if not hasattr(arch, 'frequence') else arch.frequence))

        velocity_system = VelocitySystem(self, 'system', parser)

        self.bind(clock, 'out', velocity_system, 'clock')
