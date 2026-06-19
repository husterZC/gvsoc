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
        self.num_chip                            = 1
        self.num_cluster                         = 4

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

        self.rdma_reg_offset                     = 0x00000200
        self.rdma_reg_size                       = 0x00000100
        self.rdma_bus_width                      = self.dma_bus_width
        self.rdma_read_buffer_size               = self.dma_read_buffer_size
        self.rdma_write_buffer_size              = self.dma_write_buffer_size
        self.rdma_max_inflight_txn               = self.dma_max_inflight_txn
        self.rdma_base_latency                   = self.dma_base_latency
        self.rdma_chip_stride                    = 0x00010000

        # The default app example exercises in-router collectives, so it needs
        # the unified interconnect path enabled.
        self.onchip                              = "fat_tree"
        self.onchip_topology                     = "fat_tree"
        self.onchip_link_latency                 = 1
        self.onchip_link_width                   = self.dma_bus_width
        self.onchip_link_pending_size            = self.dma_write_buffer_size
        self.onchip_router_pending_size          = self.dma_write_buffer_size
        self.onchip_collective_buffer_size       = 65536
        self.onchip_collective_max_pending       = 1024
        self.onchip_collective_alu_count         = self.dma_bus_width
        self.onchip_collective_alu_latency       = 1

        # Default fat-tree parameters. Other topology parameters should be set
        # by topology-specific arch files or generated regression arch files.
        self.onchip_tree_radix                   = 8
        self.onchip_tree_level                   = 3

        self.offchip                             = "ring"
        self.offchip_topology                    = "ring"
        self.offchip_link_latency                = 1
        self.offchip_link_width                  = self.rdma_bus_width
        self.offchip_link_pending_size           = self.rdma_write_buffer_size
        self.offchip_router_pending_size         = self.rdma_write_buffer_size
        self.offchip_collective_buffer_size      = 65536
        self.offchip_collective_max_pending      = 1024
        self.offchip_collective_alu_count        = self.rdma_bus_width
        self.offchip_collective_alu_latency      = 1
        self.offchip_ring_size                   = self.num_chip
        self.offchip_tree_radix                  = 2
        self.offchip_tree_level                  = 1

        self.cluster_tcdm_base                   = 0x00000000
        self.cluster_tcdm_size                   = 0x00100000

        self.cluster_stack_base                  = 0x10000000
        self.cluster_stack_size                  = 0x00020000

        self.cluster_zomem_base                  = 0x18000000
        self.cluster_zomem_size                  = 0x00020000

        self.cluster_reg_base                    = 0x20000000
        self.cluster_reg_size                    = 0x00000300

        self.instruction_mem_base                = 0x80000000
        self.instruction_mem_size                = 0x00010000

        #System
        self.soc_register_base                   = 0x70000000
        self.soc_register_size                   = 0x00010000
        self.soc_register_eoc                    = 0x70000000
