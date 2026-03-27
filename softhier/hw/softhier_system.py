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
from pulp.chips.softhier.cluster_unit import ClusterUnit, ClusterArch
from pulp.chips.softhier.softhier_ctrl import SoftHierCtrl
from pulp.chips.softhier.softhier_arch import SoftHierArch
import math

class SoftHierSystem(gvsoc.systree.Component):

    def __init__(self, parent, name, parser):
        super().__init__(parent, name)

        #################
        # Configuration #
        #################

        arch = SoftHierArch()

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
            cluster_arch = ClusterArch( num_core 		    = arch.num_core_per_cluster,
            							cluster_id 			= cluster_id,
        								spatz_num_lane		= arch.spatz_num_lane,
        								spatz_lane_width	= arch.spatz_lane_width,
                                        tcdm_bank_nb        = arch.cluster_tcdm_bank_nb,
                                        tcdm_bank_width     = arch.cluster_tcdm_bank_width,
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
        								idma_outstand_txn 	= arch.idma_outstand_txn,
        								idma_outstand_burst = arch.idma_outstand_burst)
            cluster_list.append(ClusterUnit(self,f'cluster_{cluster_id}', cluster_arch, binary))
            pass

        #Virtual router, just for debugging and non-performance-critical jobs
        narrow_interco = router.Router(self, 'narrow_interco', bandwidth=8)

        #Debug Memory
        debug_mem = memory.memory.Memory(self,'debug_mem', size=1)

        #Control register
        softhier_ctrl = SoftHierCtrl(self, 'softhier_ctrl', num_cluster=arch.num_cluster)

        ############
        # Bindings #
        ############

        #Debug memory
        narrow_interco.o_MAP(debug_mem.i_INPUT())

        #Control register
        narrow_interco.o_MAP(softhier_ctrl.i_INPUT(), base=arch.soc_register_base, size=arch.soc_register_size, rm_base=True)

        #Clusters
        for cluster_id in range(arch.num_cluster):
            cluster_list[cluster_id].o_NARROW_SOC(narrow_interco.i_INPUT())
            pass


class SoftHierPlatform(gvsoc.systree.Component):

    def __init__(self, parent, name, parser, options):
        super(SoftHierPlatform, self).__init__(parent, name, options=options)

        arch  = SoftHierArch()
        clock = Clock_domain(self, 'clock', frequency=(1000000000 if not hasattr(arch, 'frequence') else arch.frequence))

        softhier_system = SoftHierSystem(self, 'system', parser)

        self.bind(clock, 'out', softhier_system, 'clock')
