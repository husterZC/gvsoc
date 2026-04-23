#
# Copyright (C) 2020 GreenWaves Technologies, SAS, ETH Zurich and University of Bologna
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

class ClusterCSR(gvsoc.systree.Component):

    def __init__(self, parent, name, bus_granularity=64):
        super(ClusterCSR, self).__init__(parent, name)

        self.add_sources(['pulp/chips/velocity/cluster_csr.cpp'])

        self.add_properties({
            'bus_granularity': bus_granularity,
        })

    def i_INPUT(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'input', signature='io')
    
    def o_TCDM(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind(f'tcdm', itf, signature='io')

    def o_BUS_OUTPUT(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind(f'bus_output', itf, signature='io')
    
    def i_BUS_INPUT(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'bus_input', signature='io')

    def i_LOCK_REQ(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, f'lock_req', signature='wire<bool>')
    
    def o_LOCK_ACK(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind(f'lock_ack', itf, signature='wire<bool>')

