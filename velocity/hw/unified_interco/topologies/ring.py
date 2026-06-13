from pulp.chips.velocity.unified_interco.topologies.base import GraphTopologyInterconnect
from pulp.chips.velocity.unified_interco.topologies.utils import get_int_attr


class RingInterconnect(GraphTopologyInterconnect):
    def __init__(self, parent, name, clusters, arch):
        self.size = get_int_attr(
            arch,
            ['unified_interco_ring_size', 'unified_interco_endpoints'],
            default=arch.num_cluster,
        )
        if self.size <= 0:
            raise ValueError('ring interconnect requires ring size > 0')

        self.nodes_by_index = {}
        self.routing_mode = getattr(arch, 'unified_interco_ring_routing',
                                    getattr(arch, 'unified_interco_routing', 'wrap_tree'))
        if not isinstance(self.routing_mode, str):
            raise ValueError('ring routing mode must be a string')
        self.routing_mode = self.routing_mode.lower().replace('-', '_')
        if self.routing_mode not in ('wrap_tree', 'tree', 'minimal', 'wrap_minimal'):
            raise ValueError(f'ring unsupported routing mode: {self.routing_mode}')

        super().__init__(parent, name, clusters, arch, 'ring')
        self._check_capacity(self.size)
        self._build()
        if self.routing_mode in ('minimal', 'wrap_minimal'):
            self._finalize_next_hop_routes(self._minimal_ring_next_hop)
        else:
            self._finalize_spanning_tree_routes(self.nodes_by_index[0])

    def _build(self):
        for index in range(self.size):
            self.nodes_by_index[index] = self._new_node(f'router_{index}', coord=index)

        for cluster_id in range(self.num_cluster):
            self._connect_cluster(cluster_id, self.nodes_by_index[cluster_id])

        if self.size == 1:
            return

        for index in range(self.size - 1):
            self._connect_router_pair(self.nodes_by_index[index], self.nodes_by_index[index + 1])
        self._connect_router_pair(self.nodes_by_index[self.size - 1], self.nodes_by_index[0])

    def _minimal_ring_next_hop(self, node, cluster_id: int, target):
        current = node.coord
        dst = target.coord
        forward = (dst - current) % self.size
        backward = (current - dst) % self.size
        if forward <= backward:
            return self.nodes_by_index[(current + 1) % self.size]
        return self.nodes_by_index[(current - 1) % self.size]
