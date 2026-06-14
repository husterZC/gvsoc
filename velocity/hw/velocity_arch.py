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

class VelocityArch:

    def __init__(self):

        #Cluster
        self.num_cluster                         = 128

        self.cluster_num_lane                    = 512
        self.cluster_lane_width                  = 4

        self.dma_reg_offset                      = 0x00000100
        self.dma_reg_size                        = 0x00000100
        self.dma_bus_width                       = 256
        self.dma_read_buffer_size                = 4096
        self.dma_write_buffer_size               = 4096
        self.dma_max_inflight_txn                = 256
        self.dma_base_latency                    = 1
        self.dma_cluster_stride                  = 0x00010000

        # Default keeps the legacy flat DMA interconnect. Set unified_interco
        # to a topology name to enable velocity.hw.unified_interco.
        self.unified_interco                     = None
        self.unified_interco_topology            = "fat_tree"
        self.unified_interco_link_latency        = 1
        self.unified_interco_link_width          = self.dma_bus_width
        self.unified_interco_link_pending_size   = self.dma_write_buffer_size
        self.unified_interco_router_pending_size = self.dma_write_buffer_size
        self.unified_interco_collective_buffer_size = 65536
        self.unified_interco_collective_max_pending = 1024
        self.unified_interco_collective_alu_count = self.dma_bus_width
        self.unified_interco_collective_alu_latency = 1

        # Default fat-tree parameters. Other topology parameters should be set
        # by topology-specific arch files or generated regression arch files.
        self.unified_interco_tree_radix          = 8
        self.unified_interco_tree_level          = 3

        self.cluster_tcdm_base                   = 0x00000000
        self.cluster_tcdm_size                   = 0x00100000

        self.cluster_stack_base                  = 0x10000000
        self.cluster_stack_size                  = 0x00020000

        self.cluster_zomem_base                  = 0x18000000
        self.cluster_zomem_size                  = 0x00020000

        self.cluster_reg_base                    = 0x20000000
        self.cluster_reg_size                    = 0x00000200

        self.instruction_mem_base                = 0x80000000
        self.instruction_mem_size                = 0x00010000

        #System
        self.soc_register_base                   = 0x70000000
        self.soc_register_size                   = 0x00010000
        self.soc_register_eoc                    = 0x70000000
