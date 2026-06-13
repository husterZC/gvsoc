from pulp.chips.velocity.unified_interco.topologies.base import GraphTopologyInterconnect
from pulp.chips.velocity.unified_interco.topologies.utils import ceil_log2, get_int_attr


class HypercubeInterconnect(GraphTopologyInterconnect):
    def __init__(self, parent, name, clusters, arch):
        self.ndim = get_int_attr(
            arch,
            ['unified_interco_hypercube_dims', 'unified_interco_ndim'],
            default=max(1, ceil_log2(arch.num_cluster)),
        )
        if self.ndim <= 0:
            raise ValueError('hypercube interconnect requires N dims > 0')

        self.capacity = 1 << self.ndim
        self.nodes_by_index = {}

        super().__init__(parent, name, clusters, arch, 'hypercube')
        self._check_capacity(self.capacity)
        self._build()
        self._finalize_next_hop_routes(self._e_cube_next_hop)

    def _build(self):
        for index in range(self.capacity):
            self.nodes_by_index[index] = self._new_node(f'router_{index}', coord=index)

        for cluster_id in range(self.num_cluster):
            self._connect_cluster(cluster_id, self.nodes_by_index[cluster_id])

        for index in range(self.capacity):
            for bit in range(self.ndim):
                peer = index ^ (1 << bit)
                if index < peer:
                    self._connect_router_pair(self.nodes_by_index[index], self.nodes_by_index[peer])

    def _e_cube_next_hop(self, node, cluster_id: int, target):
        diff = node.coord ^ target.coord
        if diff == 0:
            raise ValueError(f'hypercube router {node.name} is already at destination {cluster_id}')
        bit = (diff & -diff).bit_length() - 1
        return self.nodes_by_index[node.coord ^ (1 << bit)]
