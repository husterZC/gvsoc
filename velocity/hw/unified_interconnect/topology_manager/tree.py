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
from pulp.chips.velocity.unified_interconnect.d2dlink import D2DLink
from pulp.chips.velocity.unified_interconnect.router import Router
import math

'''
Tree topology manager
Global information is provided as a list of dict, one per level, with the following keys:
- num_sub   : number of subtree at this level's node
- link_type : type of link to connect the subtree (e.g., die-to-die, on-chip)
- onchip_cfg : configuration of the on-chip link (if link_type is onchip), e.g., bandwidth, latency, etc.
- d2d_cfg   : configuration of the die-to-die link (if link_type is d2d):
'''
class Tree(gvsoc.systree.Component):
    def __init__(self, parent, name, global_info, at_level = 0, coordinates = ''):
        super(Tree, self).__init__(parent, name)
        self.global_info = global_info
        self.num_level = len(global_info)
        self.levels = [global_info[i]['num_sub'] for i in range(self.num_level)]
        self.add_properties({
            'levels': ",".join([str(x) for x in self.levels]),
            'num_level': self.num_level,
            'num_sub': self.global_info[at_level]['num_sub'],
            'at_level': at_level,
            'coordinates': coordinates,
            'collective_queue_depth': 2 * self.global_info[at_level]['num_sub'],
            'collective_reduce_latency': math.ceil(math.log2(self.global_info[at_level]['num_sub'])),
        })
        self.add_sources(['pulp/chips/velocity/unified_interconnect/topology_manager/tree.cpp'])

    def i_TOP_INPUT(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, f'in', signature='io')
    
    def o_TOP_OUT(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind(f'out', itf, signature='io')

    def assert_global_info(self, global_info):
        assert isinstance(global_info, list), "global_info must be a list of dict"
        assert len(global_info) > 0, "global_info must contain at least one level"
        # Check that the global information is consistent (e.g., num_sub > 0, link_bw > 0, etc.)
        for level_info in global_info:
            if level_info['link_type'] == 'onchip':
                onchip_cfg = level_info['onchip_cfg']
                assert onchip_cfg['link_latency_cycle'] >= 0, "on-chip link_latency_cycle must be non-negative"
                assert onchip_cfg['link_width_Bytes'] > 0, "on-chip link_width_Bytes must be greater than 0"
                assert onchip_cfg['link_granularity_Bytes'] > 0, "on-chip link_granularity_Bytes must be greater than 0"
            if level_info['link_type'] == 'd2d':
                d2d_cfg = level_info['d2d_cfg']
                assert d2d_cfg['link_depth_rx'] > 0, "d2d link_depth_rx must be greater than 0"
                assert d2d_cfg['link_depth_tx'] > 0, "d2d link_depth_tx must be greater than 0"
                assert d2d_cfg['link_credit_bar'] >= 0, "d2d link_credit_bar must be non-negative"
                assert d2d_cfg['flit_granularity_byte'] > 0, "d2d flit_granularity_byte must be greater than 0"
                assert d2d_cfg['link_latency_ns'] >= 0, "d2d link_latency_ns must be non-negative"
                assert d2d_cfg['link_bandwidth_GBps'] > 0, "d2d link_bandwidth_GBps must be greater than 0"
        pass
    
    def corr2id(self, coordinates_list, global_info):
        # Convert coordinates to a unique ID based on the global information
        id = 0
        multiplier = 1
        for i in reversed(range(len(coordinates_list))):
            id += coordinates_list[i] * multiplier
            multiplier *= global_info[i]['num_sub']
        return id

    def build_tree(self, parent, at_level, coordinates_list, global_info, leaf_list):
        # 1. Create the router
        router = Router(parent, f"router_{'_'.join([str(x) for x in coordinates_list])}",
                        num_port=global_info[at_level]['num_sub'] + 1, rx_depth=2)

        # 2. Create the manager
        top = Tree(parent, f"tree_{'_'.join([str(x) for x in coordinates_list])}",
                    global_info=global_info, at_level=at_level, coordinates=",".join([str(x) for x in coordinates_list]))

        # 3. Connect the router to the manager
        router.o_TOP_OUT(top.i_TOP_INPUT())
        top.o_TOP_OUT(router.i_TOP_INPUT())

        # 4. Get the list of subtrees
        subtree_list = []
        if at_level >= len(global_info) - 1:
            # The last level router, connect it to the leaf and return
            for i in range(global_info[at_level]['num_sub']):
                subtree_list.append(leaf_list[self.corr2id(coordinates_list + [i], global_info)])
                pass
            pass
        else:
            # Internal node, create subtrees and connect them with the appropriate links
            for i in range(global_info[at_level]['num_sub']):
                subtree = self.build_tree(parent, at_level + 1, coordinates_list + [i], global_info, leaf_list)
                subtree_list.append(subtree)
                pass
            pass

        #5. Connect the subtrees to the router
        if(global_info[at_level]['link_type'] == 'd2d'):
            for i in range(global_info[at_level]['num_sub']):
                link_r2s = D2DLink(parent, f"d2dlink_r2s_{'_'.join([str(x) for x in coordinates_list])}_to_{i}",
                    link_id=0, # Not used for now
                    fifo_depth_rx=global_info[at_level]["d2d_cfg"]["link_depth_rx"],
                    fifo_depth_tx=global_info[at_level]["d2d_cfg"]["link_depth_tx"],
                    fifo_credit_bar=global_info[at_level]["d2d_cfg"]["link_credit_bar"],
                    flit_granularity_byte=global_info[at_level]["d2d_cfg"]["flit_granularity_byte"],
                    link_latency_ns=global_info[at_level]["d2d_cfg"]["link_latency_ns"],
                    link_bandwidth_GBps=global_info[at_level]["d2d_cfg"]["link_bandwidth_GBps"])
                link_s2r = D2DLink(parent, f"d2dlink_s2r_{'_'.join([str(x) for x in coordinates_list])}_from_{i}",
                    link_id=0, # Not used for now
                    fifo_depth_rx=global_info[at_level]["d2d_cfg"]["link_depth_rx"],
                    fifo_depth_tx=global_info[at_level]["d2d_cfg"]["link_depth_tx"],
                    fifo_credit_bar=global_info[at_level]["d2d_cfg"]["link_credit_bar"],
                    flit_granularity_byte=global_info[at_level]["d2d_cfg"]["flit_granularity_byte"],
                    link_latency_ns=global_info[at_level]["d2d_cfg"]["link_latency_ns"],
                    link_bandwidth_GBps=global_info[at_level]["d2d_cfg"]["link_bandwidth_GBps"])
                router.o_PORT_OUT(link_r2s.i_DATA_INPUT(), i+1)
                link_r2s.o_DATA_OUTPUT(subtree_list[i].i_PORT_INPUT(0))
                subtree_list[i].o_PORT_OUT(link_s2r.i_DATA_INPUT(), 0)
                link_s2r.o_DATA_OUTPUT(router.i_PORT_INPUT(i+1))
                pass
            pass
        elif(global_info[at_level]['link_type'] == 'onchip'):
            for i in range(global_info[at_level]['num_sub']):
                router.o_PORT_OUT(subtree_list[i].i_PORT_INPUT(0), i+1)
                subtree_list[i].o_PORT_OUT(router.i_PORT_INPUT(i+1), 0)
                pass
            pass
        else:
            raise ValueError(f"Unsupported link type: {global_info[at_level]['link_type']}")

        #6. Return the router
        return router

    def build_top(self, parent, global_info, root, leaf_list):
        self.assert_global_info(global_info)

        # Build the tree recursively
        top_router = self.build_tree(parent, at_level=0, coordinates_list=[], global_info=global_info, leaf_list=leaf_list)

        # Link the top router to the root
        top_router.o_PORT_OUT(root.i_PORT_INPUT(0), 0)
        root.o_PORT_OUT(top_router.i_PORT_INPUT(0), 0)
        pass
