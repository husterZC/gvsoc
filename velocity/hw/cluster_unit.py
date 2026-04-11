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

import gvsoc.runner
import pulp.snitch.snitch_core as iss
import memory.memory as memory
import interco.router as router
import gvsoc.systree
from pulp.snitch.snitch_cluster.dma_interleaver import DmaInterleaver
from pulp.snitch.zero_mem import ZeroMem
from elftools.elf.elffile import *
from pulp.idma.snitch_dma import SnitchDma
from pulp.cluster.l1_interleaver import L1_interleaver
import gvsoc.runner
import math
from pulp.snitch.sequencer import Sequencer
import utils.loader.loader


GAPY_TARGET = True

#Function to get EoC entry
def find_binary_entry(elf_filename):
    # Open the ELF file in binary mode
    with open(elf_filename, 'rb') as f:
        elffile = ELFFile(f)

        # Find the symbol table section in the ELF file
        for section in elffile.iter_sections():
            if isinstance(section, SymbolTableSection):
                # Iterate over symbols in the symbol table
                for symbol in section.iter_symbols():
                    # Check if this symbol's name matches "tohost"
                    if symbol.name == '_start':
                        # Return the symbol's address
                        return symbol['st_value']

    # If the symbol wasn't found, return None
    return None


class ClusterArch:
    def __init__(
        self,  
        num_cluster,        cluster_id,
        num_lane,           lane_width,
        inst_base,          inst_size,
        tcdm_base,          tcdm_size,
        stack_base,         stack_size,
        zomem_base,         zomem_size,
        reg_base,           reg_size,
        idma_outstand_txn,  idma_outstand_burst,
        auto_fetch=False):

        self.num_cluster            = num_cluster
        self.cluster_id             = cluster_id
        self.num_lane               = num_lane
        self.lane_width             = lane_width
        self.inst_base              = inst_base
        self.inst_size              = inst_size
        self.tcdm_base              = tcdm_base
        self.tcdm_size              = tcdm_size
        self.stack_base             = stack_base
        self.stack_size             = stack_size
        self.zomem_base             = zomem_base
        self.zomem_size             = zomem_size
        self.reg_base               = reg_base
        self.reg_size               = reg_size
        self.idma_outstand_txn      = idma_outstand_txn
        self.idma_outstand_burst    = idma_outstand_burst
        self.auto_fetch             = auto_fetch


class ClusterTcdm(gvsoc.systree.Component):

    def __init__(self, parent, name, arch):
        super().__init__(parent, name)

        banks = []
        nb_banks = arch.num_lane
        bank_size = (arch.tcdm_size / arch.num_lane) + arch.lane_width
        nb_masters = 1 + arch.num_lane
        for i in range(0, nb_banks):
            banks.append(memory.Memory(self, f'bank_{i}', size=bank_size, atomics=True, width_log2=int(math.log2(arch.lane_width))))

        interleaver = L1_interleaver(self, 'interleaver', nb_slaves=nb_banks,
            nb_masters=nb_masters, interleaving_bits=int(math.log2(arch.lane_width)))

        for i in range(0, nb_banks):
            self.bind(interleaver, 'out_%d' % i, banks[i], 'input')

        for i in range(0, nb_masters):
            self.bind(self, f'in_{i}', interleaver, f'in_{i}')

    def i_INPUT(self, port: int) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, f'in_{port}', signature='io')



class ClusterUnit(gvsoc.systree.Component):

    def __init__(self, parent, name, arch, binary):
        super().__init__(parent, name)

        #
        # Components
        #

        # Boot Address
        boot_addr = 0x8000_0000
        if binary is not None:
            boot_addr = find_binary_entry(binary)

        # Loader
        loader = utils.loader.loader.ElfLoader(self, 'loader', binary=binary)

        # Instruction memory
        instr_mem = memory.Memory(self, 'instr_mem', size=arch.inst_size, atomics=True, width_log2=-1)

        #Instruction router
        instr_router = router.Router(self, 'instr_router', bandwidth=8)

        # Core
        core = iss.SnitchFast(self, f'core', isa='rv32imfdva',
                    fetch_enable=arch.auto_fetch, boot_addr=boot_addr,
                    core_id=arch.cluster_id, htif=False,
                    inc_spatz=True,
                    spatz_nb_lanes=arch.num_lane,
                    spatz_lane_width=arch.lane_width
                )

        # TCDM
        tcdm = ClusterTcdm(self, 'tcdm', arch)

        # Stack memory
        stack_mem = memory.Memory(self, 'stack_mem', size=arch.stack_size)

        # Core interco
        core_ico = router.Router(self, f'core_ico', bandwidth=8)


        #
        # Bindings
        #

        # Binary loader
        loader.o_OUT(instr_router.i_INPUT())
        loader.o_START(core.i_FETCHEN())
        instr_router.o_MAP(instr_mem.i_INPUT(), base=arch.inst_base, size=arch.inst_size, rm_base=True)

        # Core
        core.o_DATA(core_ico.i_INPUT())
        core.o_FETCH(instr_router.i_INPUT())
        for lane in range(arch.num_lane):
            vlsu_router = router.Router(self, f'spatz_lane{lane}_router', bandwidth=arch.lane_width)
            vlsu_router.add_mapping("output")
            core.o_VLSU(lane, vlsu_router.i_INPUT())
            self.bind(vlsu_router, 'output', tcdm, f'in_{lane}')

        # Core interco
        core_ico.o_MAP(self.i_VIRTUAL_SOC())
        core_ico.o_MAP(instr_router.i_INPUT(),         base=arch.inst_base,    size=arch.inst_size,    rm_base=False)
        core_ico.o_MAP(stack_mem.i_INPUT(),            base=arch.stack_base,   size=arch.stack_size,   rm_base=True)
        core_ico.o_MAP(tcdm.i_INPUT(arch.num_lane),    base=arch.tcdm_base,    size=arch.tcdm_size,    rm_base=True)


    def i_VIRTUAL_SOC(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'virtual_soc', signature='io')

    def o_VIRTUAL_SOC(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('virtual_soc', itf, signature='io')


