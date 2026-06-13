from pulp.chips.velocity.unified_interco.topologies.base import GraphTopologyInterconnect
from pulp.chips.velocity.unified_interco.topologies.utils import get_int_attr


class TreeInterconnect(GraphTopologyInterconnect):
    def __init__(self, parent, name, clusters, arch):
        self.radix = get_int_attr(
            arch,
            ['unified_interco_tree_radix'],
            default=2,
        )
        self.level = get_int_attr(
            arch,
            ['unified_interco_tree_level'],
            default=1,
        )

        if self.radix <= 0:
            raise ValueError('tree interconnect requires radix > 0')
        if self.level <= 0:
            raise ValueError('tree interconnect requires level > 0')

        self.capacity = self.radix ** self.level
        self.nodes_by_path = {}

        super().__init__(parent, name, clusters, arch, 'tree')
        self._check_capacity(self.capacity)
        self._build()
        self._finalize_spanning_tree_routes(self.nodes_by_path[()])

    def _build(self):
        for depth in range(self.level):
            for index in range(self.radix ** depth):
                path = self._index_to_path(index, depth)
                self.nodes_by_path[path] = self._new_node(self._node_name(path), coord=path)

        for depth in range(1, self.level):
            for index in range(self.radix ** depth):
                path = self._index_to_path(index, depth)
                parent_path = path[:-1]
                self._connect_router_pair(self.nodes_by_path[parent_path], self.nodes_by_path[path])

        for cluster_id in range(self.num_cluster):
            leaf_digits = self._index_to_path(cluster_id, self.level)
            leaf_path = leaf_digits[:-1]
            self._connect_cluster(cluster_id, self.nodes_by_path[leaf_path])

    def _index_to_path(self, index: int, length: int) -> tuple[int, ...]:
        digits = [0] * length
        for pos in range(length - 1, -1, -1):
            digits[pos] = index % self.radix
            index //= self.radix
        return tuple(digits)

    def _node_name(self, path: tuple[int, ...]) -> str:
        if not path:
            return 'root'
        return 'level_' + str(len(path)) + '_' + '_'.join(str(value) for value in path)
