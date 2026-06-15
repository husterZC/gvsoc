from collections import defaultdict, deque
from dataclasses import dataclass

import gvsoc.systree

from pulp.chips.velocity.unified_interco.link import UnifiedLink
from pulp.chips.velocity.unified_interco.router import UnifiedRouter
from pulp.chips.velocity.unified_interco.topologies.utils import get_int_attr


@dataclass
class _RouterNode:
    name: str
    level: int
    coord: tuple[int, ...]
    router_id: int
    routes: list[int]
    next_input: int = 0
    next_output: int = 0
    component: UnifiedRouter | None = None
    output_clusters: dict[int, int] = None


class FatTreeInterconnect(gvsoc.systree.Component):
    def __init__(self, parent, name, clusters, arch):
        super().__init__(parent, name)

        self.clusters = clusters
        self.num_cluster = arch.num_cluster
        self.cluster_stride = arch.dma_cluster_stride
        self.radix = get_int_attr(arch, ['unified_interco_tree_radix'], default=8)
        self.level = get_int_attr(arch, ['unified_interco_tree_level'], default=3)
        self.link_latency = getattr(arch, 'unified_interco_link_latency', 1)
        self.link_width = getattr(arch, 'unified_interco_link_width', arch.dma_bus_width)
        self.link_pending_size = getattr(arch, 'unified_interco_link_pending_size', arch.dma_write_buffer_size)
        self.router_pending_size = getattr(arch, 'unified_interco_router_pending_size', arch.dma_write_buffer_size)
        self.collective_buffer_size = getattr(arch, 'unified_interco_collective_buffer_size', 65536)
        self.collective_max_pending = getattr(arch, 'unified_interco_collective_max_pending', 1024)
        self.collective_alu_count = getattr(arch, 'unified_interco_collective_alu_count', self.link_width)
        self.collective_alu_latency = getattr(arch, 'unified_interco_collective_alu_latency', 1)

        self._sanity_check_basic()

        self.down_ports = self.radix if self.level == 1 else (self.radix + 1) // 2
        self.up_ports = 0 if self.level == 1 else self.radix - self.down_ports
        self.capacity = self._capacity()

        if self.num_cluster > self.capacity:
            raise ValueError(
                f'Fat-tree radix {self.radix} level {self.level} supports at most '
                f'{self.capacity} clusters, got {self.num_cluster}'
            )

        self.nodes: dict[tuple[int, tuple[int, ...]], _RouterNode] = {}
        self.node_order: list[_RouterNode] = []
        self._links: list[UnifiedLink] = []
        self._router_to_link_bindings: list[tuple[_RouterNode, int, UnifiedLink]] = []
        self._link_to_router_bindings: list[tuple[UnifiedLink, _RouterNode, int]] = []
        self._graph: dict[str, list[tuple[str, int]]] = defaultdict(list)
        self._cluster_router: dict[int, _RouterNode] = {}
        self._collective_subtrees: dict[str, list[int]] = {}
        self._collective_subtree_words_per_root = 0

        if self.level == 1:
            self._build_level1()
        else:
            self._build_multi_level()

        self._compute_routes()
        self._instantiate_routers()

    def _sanity_check_basic(self):
        if self.num_cluster <= 0:
            raise ValueError('Fat-tree unified interconnect requires num_cluster > 0')
        if self.cluster_stride <= 0:
            raise ValueError('Fat-tree unified interconnect requires dma_cluster_stride > 0')
        if self.radix <= 0:
            raise ValueError('Fat-tree unified interconnect requires radix > 0')
        if self.level <= 0:
            raise ValueError('Fat-tree unified interconnect requires level > 0')
        if self.level > 1 and self.radix < 2:
            raise ValueError('Fat-tree unified interconnect requires radix >= 2 when level > 1')
        if self.link_latency < 0:
            raise ValueError('Fat-tree unified interconnect requires link latency >= 0')
        if self.link_width <= 0:
            raise ValueError('Fat-tree unified interconnect requires link width > 0')
        if self.link_pending_size < 0 or self.router_pending_size < 0:
            raise ValueError('Fat-tree unified interconnect pending sizes must be >= 0')

    def _capacity(self) -> int:
        if self.level == 1:
            return self.radix
        return self.radix * (self.down_ports ** (self.level - 1))

    def _new_node(self, level: int, coord: tuple[int, ...]) -> _RouterNode:
        key = (level, coord)
        node = self.nodes.get(key)
        if node is not None:
            return node

        coord_name = '_'.join(str(value) for value in coord) if coord else '0'
        node = _RouterNode(
            name=f'level_{level}_{coord_name}',
            level=level,
            coord=coord,
            router_id=len(self.node_order),
            routes=[-1] * self.num_cluster,
            output_clusters={},
        )
        self.nodes[key] = node
        self.node_order.append(node)
        return node

    def _alloc_input(self, node: _RouterNode) -> int:
        port = node.next_input
        node.next_input += 1
        self._check_port(node, port, 'input')
        return port

    def _alloc_output(self, node: _RouterNode) -> int:
        port = node.next_output
        node.next_output += 1
        self._check_port(node, port, 'output')
        return port

    def _check_port(self, node: _RouterNode, port: int, direction: str):
        if port >= self.radix:
            raise ValueError(
                f'Fat-tree router {node.name} exceeds radix {self.radix} on {direction} ports'
            )

    def _new_link(self, name: str) -> UnifiedLink:
        link = UnifiedLink(
            self,
            name,
            latency=self.link_latency,
            width=self.link_width,
            max_pending_size=self.link_pending_size,
        )
        self._links.append(link)
        return link

    def _connect_router_to_router(self, src: _RouterNode, dst: _RouterNode):
        out_port = self._alloc_output(src)
        in_port = self._alloc_input(dst)
        link = self._new_link(f'{src.name}_to_{dst.name}')
        self._router_to_link_bindings.append((src, out_port, link))
        self._link_to_router_bindings.append((link, dst, in_port))
        self._graph[src.name].append((dst.name, out_port))

    def _connect_router_pair(self, lower: _RouterNode, upper: _RouterNode):
        self._connect_router_to_router(lower, upper)
        self._connect_router_to_router(upper, lower)

    def _connect_cluster(self, cluster_id: int, leaf: _RouterNode):
        in_port = self._alloc_input(leaf)
        uplink = self._new_link(f'cluster_{cluster_id}_to_{leaf.name}')
        self.bind(self, f'cluster_{cluster_id}_in', uplink, 'input')
        self._link_to_router_bindings.append((uplink, leaf, in_port))

        out_port = self._alloc_output(leaf)
        downlink = self._new_link(f'{leaf.name}_to_cluster_{cluster_id}')
        self._router_to_link_bindings.append((leaf, out_port, downlink))
        downlink.o_OUTPUT(gvsoc.systree.SlaveItf(self, f'cluster_{cluster_id}_out', signature='io'))
        self._graph[leaf.name].append((self._cluster_sink(cluster_id), out_port))
        leaf.output_clusters[out_port] = cluster_id
        self._cluster_router[cluster_id] = leaf

    def _cluster_sink(self, cluster_id: int) -> str:
        return f'cluster_{cluster_id}'

    def _build_level1(self):
        root = self._new_node(0, ())
        for cluster_id in range(self.num_cluster):
            self._connect_cluster(cluster_id, root)

    def _build_multi_level(self):
        cluster_coords = [self.locate_cluster(cluster_id) for cluster_id in range(self.num_cluster)]

        for cluster_id, coord in enumerate(cluster_coords):
            leaf = self._new_node(0, self._leaf_coord(coord))
            self._connect_cluster(cluster_id, leaf)

        for lower_level in range(self.level - 1):
            for lower_coord in self._active_coords(lower_level):
                lower = self._new_node(lower_level, lower_coord)
                for up_index in range(self.up_ports):
                    upper_level = lower_level + 1
                    upper_coord = self._upper_coord(lower_level, lower_coord, up_index)
                    upper = self._new_node(upper_level, upper_coord)
                    self._connect_router_pair(lower, upper)

    def _active_coords(self, level: int) -> list[tuple[int, ...]]:
        return sorted(coord for node_level, coord in self.nodes if node_level == level)

    def locate_cluster(self, cluster: int) -> tuple[int, tuple[int, ...]]:
        if self.level == 1:
            return cluster, ()

        local_capacity = self.down_ports ** (self.level - 1)
        pod = cluster // local_capacity
        local = cluster % local_capacity

        digits = []
        for power in range(self.level - 2, -1, -1):
            divisor = self.down_ports ** power
            digits.append(local // divisor)
            local %= divisor

        return pod, tuple(digits)

    def _leaf_coord(self, cluster_coord: tuple[int, tuple[int, ...]]) -> tuple[int, ...]:
        pod, digits = cluster_coord
        return (pod,) + digits[:-1]

    def _upper_coord(self, lower_level: int, lower_coord: tuple[int, ...], up_index: int) -> tuple[int, ...]:
        lower_down_count = self.level - 2 - lower_level
        upper_down_count = self.level - 2 - (lower_level + 1)
        up_prefix = lower_coord[1 + lower_down_count:]

        if lower_level + 1 == self.level - 1:
            return up_prefix + (up_index,)

        pod = lower_coord[0]
        down_prefix = lower_coord[1:1 + upper_down_count]
        return (pod,) + down_prefix + up_prefix + (up_index,)

    def _compute_routes(self):
        reverse_graph: dict[str, list[str]] = defaultdict(list)
        for src, edges in self._graph.items():
            for dst, _ in edges:
                reverse_graph[dst].append(src)

        for cluster_id in range(self.num_cluster):
            target = self._cluster_sink(cluster_id)
            distance = {target: 0}
            queue = deque([target])

            while queue:
                node = queue.popleft()
                for predecessor in reverse_graph[node]:
                    if predecessor not in distance:
                        distance[predecessor] = distance[node] + 1
                        queue.append(predecessor)

            for node in self.node_order:
                if node.name not in distance:
                    raise ValueError(
                        f'Fat-tree router {node.name} cannot route to cluster {cluster_id}'
                    )

                for dst, port in self._graph[node.name]:
                    if distance.get(dst) == distance[node.name] - 1:
                        node.routes[cluster_id] = port
                        break

                if node.routes[cluster_id] < 0:
                    raise ValueError(
                        f'Fat-tree router {node.name} has no next hop to cluster {cluster_id}'
                    )

    def _compute_collective_subtrees(self):
        self._collective_subtree_words_per_root = (self.num_cluster + 31) // 32
        total_words = self.num_cluster * self._collective_subtree_words_per_root
        self._collective_subtrees = {
            node.name: [0] * total_words for node in self.node_order
        }

        next_by_output: dict[str, dict[int, str]] = {}
        for node in self.node_order:
            next_by_output[node.name] = {
                port: dst for dst, port in self._graph.get(node.name, [])
            }

        for root_cluster in range(self.num_cluster):
            root_sink = self._cluster_sink(root_cluster)
            for src_cluster in range(self.num_cluster):
                node = self._cluster_router[src_cluster]
                visited = set()

                while True:
                    if node.name in visited:
                        raise ValueError(
                            f'Fat-tree route cycle while tracing cluster {src_cluster} '
                            f'to root {root_cluster}'
                        )
                    visited.add(node.name)

                    word_index = (
                        root_cluster * self._collective_subtree_words_per_root +
                        src_cluster // 32
                    )
                    self._collective_subtrees[node.name][word_index] |= 1 << (src_cluster % 32)

                    output = node.routes[root_cluster]
                    dst = next_by_output[node.name].get(output)
                    if dst == root_sink:
                        break
                    if dst is None or dst.startswith('cluster_'):
                        raise ValueError(
                            f'Fat-tree route from {node.name} to root {root_cluster} '
                            f'exits through invalid destination {dst}'
                        )
                    dst_node = next((candidate for candidate in self.node_order if candidate.name == dst), None)
                    if dst_node is None:
                        raise ValueError(f'Fat-tree route references unknown router {dst}')
                    node = dst_node

    def _instantiate_routers(self):
        if not self._collective_subtrees:
            self._compute_collective_subtrees()

        for node in self.node_order:
            output_clusters = [-1] * self.radix
            for port, cluster_id in node.output_clusters.items():
                if port < self.radix:
                    output_clusters[port] = cluster_id
            node.component = UnifiedRouter(
                self,
                node.name,
                router_id=node.router_id,
                radix=self.radix,
                num_cluster=self.num_cluster,
                cluster_stride=self.cluster_stride,
                routes=node.routes,
                output_clusters=output_clusters,
                collective_subtree_words=self._collective_subtrees[node.name],
                collective_subtree_words_per_root=self._collective_subtree_words_per_root,
                max_input_pending_size=self.router_pending_size,
                collective_buffer_size=self.collective_buffer_size,
                collective_max_pending=self.collective_max_pending,
                collective_alu_count=self.collective_alu_count,
                collective_alu_latency=self.collective_alu_latency,
            )

        for node, port, link in self._router_to_link_bindings:
            node.component.o_OUTPUT(port, link.i_INPUT())
        for link, node, port in self._link_to_router_bindings:
            link.o_OUTPUT(node.component.i_INPUT(port))

    def i_CLUSTER_INPUT(self, cluster_id: int) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, f'cluster_{cluster_id}_in', signature='io')

    def o_CLUSTER_OUTPUT(self, cluster_id: int, itf: gvsoc.systree.SlaveItf):
        self.itf_bind(f'cluster_{cluster_id}_out', itf, signature='io')
