import math

from pulp.chips.velocity.unified_interco.topologies.base import GraphTopologyInterconnect
from pulp.chips.velocity.unified_interco.topologies.utils import get_int_attr


class DragonflyInterconnect(GraphTopologyInterconnect):
    def __init__(self, parent, name, clusters, arch):
        default_groups = max(1, math.ceil(math.sqrt(arch.num_cluster)))
        default_routers = max(1, math.ceil(arch.num_cluster / default_groups))

        self.groups = get_int_attr(
            arch,
            ['unified_interco_dragonfly_groups', 'unified_interco_groups'],
            default=default_groups,
        )
        self.routers_per_group = get_int_attr(
            arch,
            ['unified_interco_dragonfly_routers_per_group', 'unified_interco_routers_per_group'],
            default=default_routers,
        )
        self.terminals_per_router = get_int_attr(
            arch,
            ['unified_interco_dragonfly_terminals_per_router', 'unified_interco_terminals_per_router'],
            default=1,
        )
        self.routing_mode = getattr(arch, 'unified_interco_dragonfly_routing',
                                    getattr(arch, 'unified_interco_routing', 'tree'))
        if not isinstance(self.routing_mode, str):
            raise ValueError('dragonfly routing mode must be a string')
        self.routing_mode = self.routing_mode.lower().replace('-', '_')
        if self.routing_mode not in ('tree', 'minimal'):
            raise ValueError(f'dragonfly unsupported routing mode: {self.routing_mode}')

        self._check_params()
        self.capacity = self.groups * self.routers_per_group * self.terminals_per_router
        self.nodes_by_coord = {}

        super().__init__(parent, name, clusters, arch, 'dragonfly')
        self._check_capacity(self.capacity)
        self._build()
        if self.routing_mode == 'minimal':
            self._finalize_next_hop_routes(self._minimal_next_hop)
        else:
            self._finalize_spanning_tree_routes(self.nodes_by_coord[(0, 0)])

    def _check_params(self):
        if self.groups <= 0:
            raise ValueError('dragonfly requires groups > 0')
        if self.routers_per_group <= 0:
            raise ValueError('dragonfly requires routers_per_group > 0')
        if self.terminals_per_router <= 0:
            raise ValueError('dragonfly requires terminals_per_router > 0')

    def _build(self):
        for group in range(self.groups):
            for router in range(self.routers_per_group):
                coord = (group, router)
                self.nodes_by_coord[coord] = self._new_node(f'router_{group}_{router}', coord=coord)

        for cluster_id in range(self.num_cluster):
            router_index = cluster_id // self.terminals_per_router
            group = router_index // self.routers_per_group
            router = router_index % self.routers_per_group
            self._connect_cluster(cluster_id, self.nodes_by_coord[(group, router)])

        for group in range(self.groups):
            for left in range(self.routers_per_group):
                for right in range(left + 1, self.routers_per_group):
                    self._connect_router_pair(
                        self.nodes_by_coord[(group, left)],
                        self.nodes_by_coord[(group, right)],
                    )

        for router in range(self.routers_per_group):
            for left_group in range(self.groups):
                for right_group in range(left_group + 1, self.groups):
                    self._connect_router_pair(
                        self.nodes_by_coord[(left_group, router)],
                        self.nodes_by_coord[(right_group, router)],
                    )

    def _minimal_next_hop(self, node, cluster_id: int, target):
        group, router = node.coord
        dst_group, dst_router = target.coord

        if group == dst_group:
            return self.nodes_by_coord[(group, dst_router)]

        if router != dst_router:
            return self.nodes_by_coord[(group, dst_router)]

        return self.nodes_by_coord[(dst_group, dst_router)]
