#
# Copyright (C) 2024 ETH Zurich and University of Bologna
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

import gvsoc.systree


class VelocityDma(gvsoc.systree.Component):

    def __init__(
        self,
        parent: gvsoc.systree.Component,
        name: str,
        cluster_id: int,
        num_cluster: int,
        tcdm_size: int,
        bus_width: int,
        max_inflight: int,
        read_buffer_size: int,
        write_buffer_size: int,
        base_latency: int,
        cluster_stride: int,
    ):
        super().__init__(parent, name)

        self.add_sources(['pulp/chips/velocity/velocity_dma.cpp'])

        self.add_properties({
            'cluster_id': cluster_id,
            'num_cluster': num_cluster,
            'tcdm_size': tcdm_size,
            'bus_width': bus_width,
            'max_inflight': max_inflight,
            'read_buffer_size': read_buffer_size,
            'write_buffer_size': write_buffer_size,
            'base_latency': base_latency,
            'cluster_stride': cluster_stride,
        })

    def i_REGS(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'regs', signature='io')

    def o_TCDM(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('tcdm', itf, signature='io')

    def o_REMOTE(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('remote_out', itf, signature='io')

    def i_REMOTE(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'remote_in', signature='io')
