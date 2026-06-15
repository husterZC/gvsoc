from collections import defaultdict, deque
from dataclasses import dataclass

import gvsoc.systree

from pulp.chips.velocity.unified_interco.link import UnifiedLink
from pulp.chips.velocity.unified_interco.router import UnifiedRouter


@dataclass
class RouterNode:
    name: str
    router_id: int
    routes: list[int]
    coord: object = None
    next_input: int = 0
    next_output: int = 0
    component: UnifiedRouter | None = None
    output_clusters: dict[int, int] = None


class GraphTopologyInterconnect(gvsoc.systree.Component):
    """Common graph builder for Velocity unified interconnect topologies.

    The C++ router only needs one output port per destination cluster. This
    class handles the common plumbing: router/link instantiation, cluster
    endpoint links, route table validation, and GVSOC interface exposure.
    """

    def __init__(self, parent, name, clusters, arch, topology_name: str, router_radix: int | None = None):
        super().__init__(parent, name)

        self.clusters = clusters
        self.arch = arch
        self.topology_name = topology_name
        self.num_cluster = arch.num_cluster
        self.cluster_stride = arch.dma_cluster_stride
        self.link_latency = getattr(arch, 'unified_interco_link_latency', 1)
        self.link_width = getattr(arch, 'unified_interco_link_width', arch.dma_bus_width)
        self.link_pending_size = getattr(arch, 'unified_interco_link_pending_size', arch.dma_write_buffer_size)
        self.router_pending_size = getattr(arch, 'unified_interco_router_pending_size', arch.dma_write_buffer_size)
        self.router_radix = router_radix
        self.required_radix = 0

        self._sanity_check_common()

        self.nodes: dict[str, RouterNode] = {}
        self.node_order: list[RouterNode] = []
        self._links: list[UnifiedLink] = []
        self._router_to_link_bindings: list[tuple[RouterNode, int, UnifiedLink]] = []
        self._link_to_router_bindings: list[tuple[UnifiedLink, RouterNode, int]] = []
        self._graph: dict[str, list[tuple[str, int]]] = defaultdict(list)
        self._router_neighbors: dict[str, set[str]] = defaultdict(set)
        self._edge_ports: dict[tuple[str, str], int] = {}
        self._cluster_router: dict[int, RouterNode] = {}
        self._collective_subtrees: dict[str, list[int]] = {}
        self._collective_subtree_words_per_root = 0

    def _sanity_check_common(self):
        if self.num_cluster <= 0:
            raise ValueError(f'{self.topology_name} interconnect requires num_cluster > 0')
        if self.cluster_stride <= 0:
            raise ValueError(f'{self.topology_name} interconnect requires dma_cluster_stride > 0')
        if self.link_latency < 0:
            raise ValueError(f'{self.topology_name} interconnect requires link latency >= 0')
        if self.link_width <= 0:
            raise ValueError(f'{self.topology_name} interconnect requires link width > 0')
        if self.link_pending_size < 0 or self.router_pending_size < 0:
            raise ValueError(f'{self.topology_name} interconnect pending sizes must be >= 0')
        if self.router_radix is not None and self.router_radix <= 0:
            raise ValueError(f'{self.topology_name} interconnect requires router radix > 0')

    def _check_capacity(self, capacity: int):
        if capacity <= 0:
            raise ValueError(f'{self.topology_name} interconnect capacity must be > 0')
        if self.num_cluster > capacity:
            raise ValueError(
                f'{self.topology_name} interconnect supports at most {capacity} clusters, '
                f'got {self.num_cluster}'
            )

    def _new_node(self, name: str, coord=None) -> RouterNode:
        node = self.nodes.get(name)
        if node is not None:
            return node

        node = RouterNode(
            name=name,
            coord=coord,
            router_id=len(self.node_order),
            routes=[-1] * self.num_cluster,
            output_clusters={},
        )
        self.nodes[name] = node
        self.node_order.append(node)
        return node

    def _alloc_input(self, node: RouterNode) -> int:
        port = node.next_input
        node.next_input += 1
        self._check_port(node, port, 'input')
        return port

    def _alloc_output(self, node: RouterNode) -> int:
        port = node.next_output
        node.next_output += 1
        self._check_port(node, port, 'output')
        return port

    def _check_port(self, node: RouterNode, port: int, direction: str):
        self.required_radix = max(self.required_radix, port + 1)
        if self.router_radix is not None and port >= self.router_radix:
            raise ValueError(
                f'{self.topology_name} router {node.name} exceeds radix '
                f'{self.router_radix} on {direction} ports'
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

    def _connect_router_to_router(self, src: RouterNode, dst: RouterNode):
        if src.name == dst.name or (src.name, dst.name) in self._edge_ports:
            return

        out_port = self._alloc_output(src)
        in_port = self._alloc_input(dst)
        link = self._new_link(f'{src.name}_to_{dst.name}')
        self._router_to_link_bindings.append((src, out_port, link))
        self._link_to_router_bindings.append((link, dst, in_port))
        self._edge_ports[(src.name, dst.name)] = out_port
        self._graph[src.name].append((dst.name, out_port))
        self._router_neighbors[src.name].add(dst.name)
        self._router_neighbors[dst.name].add(src.name)

    def _connect_router_pair(self, left: RouterNode, right: RouterNode):
        self._connect_router_to_router(left, right)
        self._connect_router_to_router(right, left)

    def _connect_cluster(self, cluster_id: int, router: RouterNode):
        if cluster_id in self._cluster_router:
            raise ValueError(f'{self.topology_name} cluster {cluster_id} is already connected')

        in_port = self._alloc_input(router)
        uplink = self._new_link(f'cluster_{cluster_id}_to_{router.name}')
        self.bind(self, f'cluster_{cluster_id}_in', uplink, 'input')
        self._link_to_router_bindings.append((uplink, router, in_port))

        out_port = self._alloc_output(router)
        downlink = self._new_link(f'{router.name}_to_cluster_{cluster_id}')
        self._router_to_link_bindings.append((router, out_port, downlink))
        sink = self._cluster_sink(cluster_id)
        downlink.o_OUTPUT(gvsoc.systree.SlaveItf(self, f'cluster_{cluster_id}_out', signature='io'))
        self._edge_ports[(router.name, sink)] = out_port
        self._graph[router.name].append((sink, out_port))
        self._cluster_router[cluster_id] = router
        router.output_clusters[out_port] = cluster_id

    def _cluster_sink(self, cluster_id: int) -> str:
        return f'cluster_{cluster_id}'

    def _route_to_neighbor(self, node: RouterNode, cluster_id: int, next_node: RouterNode):
        key = (node.name, next_node.name)
        if key not in self._edge_ports:
            raise ValueError(
                f'{self.topology_name} route from {node.name} to cluster {cluster_id} '
                f'uses missing edge {node.name}->{next_node.name}'
            )
        node.routes[cluster_id] = self._edge_ports[key]

    def _route_to_local_cluster(self, node: RouterNode, cluster_id: int):
        sink = self._cluster_sink(cluster_id)
        key = (node.name, sink)
        if key not in self._edge_ports:
            raise ValueError(f'{self.topology_name} router {node.name} has no local cluster {cluster_id}')
        node.routes[cluster_id] = self._edge_ports[key]

    def _compute_routes_from_next_hop(self, next_hop):
        self._check_all_clusters_connected()

        for cluster_id, target in self._cluster_router.items():
            for node in self.node_order:
                if node.name == target.name:
                    self._route_to_local_cluster(node, cluster_id)
                    continue

                next_node = next_hop(node, cluster_id, target)
                if not isinstance(next_node, RouterNode):
                    raise ValueError(
                        f'{self.topology_name} next-hop function did not return a router '
                        f'for {node.name} -> cluster {cluster_id}'
                    )
                self._route_to_neighbor(node, cluster_id, next_node)

        self._check_routes_complete()

    def _compute_routes_shortest(self):
        self._check_all_clusters_connected()

        reverse_graph: dict[str, list[str]] = defaultdict(list)
        for src, edges in self._graph.items():
            for dst, _ in edges:
                reverse_graph[dst].append(src)

        for cluster_id in range(self.num_cluster):
            target = self._cluster_sink(cluster_id)
            distance = {target: 0}
            queue = deque([target])

            while queue:
                node_name = queue.popleft()
                for predecessor in reverse_graph[node_name]:
                    if predecessor not in distance:
                        distance[predecessor] = distance[node_name] + 1
                        queue.append(predecessor)

            for node in self.node_order:
                if node.name not in distance:
                    raise ValueError(
                        f'{self.topology_name} router {node.name} cannot route to cluster {cluster_id}'
                    )

                for dst, port in self._graph[node.name]:
                    if distance.get(dst) == distance[node.name] - 1:
                        node.routes[cluster_id] = port
                        break

                if node.routes[cluster_id] < 0:
                    raise ValueError(
                        f'{self.topology_name} router {node.name} has no next hop to cluster {cluster_id}'
                    )

    def _compute_routes_spanning_tree(self, root: RouterNode | None = None):
        self._check_all_clusters_connected()
        if not self.node_order:
            raise ValueError(f'{self.topology_name} interconnect has no routers')

        root = root or self.node_order[0]
        parents: dict[str, str | None] = {root.name: None}
        queue = deque([root.name])

        while queue:
            node_name = queue.popleft()
            for neighbor in sorted(self._router_neighbors[node_name]):
                if neighbor not in parents:
                    parents[neighbor] = node_name
                    queue.append(neighbor)

        for node in self.node_order:
            if node.name not in parents:
                raise ValueError(f'{self.topology_name} router {node.name} is disconnected')

        def next_on_tree_path(src: RouterNode, dst: RouterNode) -> RouterNode:
            if src.name == dst.name:
                return src

            src_ancestors = set()
            current = src.name
            while current is not None:
                src_ancestors.add(current)
                current = parents[current]

            down_path = []
            current = dst.name
            while current not in src_ancestors:
                down_path.append(current)
                current = parents[current]

            lca = current
            if src.name != lca:
                return self.nodes[parents[src.name]]
            return self.nodes[down_path[-1]]

        for cluster_id, target in self._cluster_router.items():
            for node in self.node_order:
                if node.name == target.name:
                    self._route_to_local_cluster(node, cluster_id)
                else:
                    self._route_to_neighbor(node, cluster_id, next_on_tree_path(node, target))

        self._check_routes_complete()

    def _check_all_clusters_connected(self):
        for cluster_id in range(self.num_cluster):
            if cluster_id not in self._cluster_router:
                raise ValueError(f'{self.topology_name} cluster {cluster_id} is not connected')

    def _check_routes_complete(self):
        for node in self.node_order:
            for cluster_id, output in enumerate(node.routes):
                if output < 0:
                    raise ValueError(
                        f'{self.topology_name} router {node.name} has no route to cluster {cluster_id}'
                    )
                if output >= self.effective_router_radix:
                    raise ValueError(
                        f'{self.topology_name} router {node.name} route to cluster {cluster_id} '
                        f'uses output {output}, radix {self.effective_router_radix}'
                    )

    def _compute_collective_subtrees(self):
        """Derive root-oriented subtree masks from the already-built routes.

        For a given root cluster R and router N, the mask contains every source
        cluster whose route to R passes through N. The C++ router uses this to
        know when an intermediate reduce/gather aggregate is complete. The
        computation is topology-agnostic: it only follows each router's existing
        route table.
        """

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
                            f'{self.topology_name} route cycle while tracing '
                            f'cluster {src_cluster} to root {root_cluster}'
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
                            f'{self.topology_name} route from {node.name} to root '
                            f'{root_cluster} exits through invalid destination {dst}'
                        )
                    node = self.nodes[dst]

    @property
    def effective_router_radix(self) -> int:
        return self.router_radix or max(self.required_radix, 1)

    def _instantiate_routers(self):
        if not self._collective_subtrees:
            self._compute_collective_subtrees()

        radix = self.effective_router_radix
        if radix < self.required_radix:
            raise ValueError(
                f'{self.topology_name} router radix {radix} is too small; '
                f'requires at least {self.required_radix}'
            )

        for node in self.node_order:
            output_clusters = [-1] * radix
            for port, cluster_id in node.output_clusters.items():
                if port < radix:
                    output_clusters[port] = cluster_id
            node.component = UnifiedRouter(
                self,
                node.name,
                router_id=node.router_id,
                radix=radix,
                num_cluster=self.num_cluster,
                cluster_stride=self.cluster_stride,
                routes=node.routes,
                output_clusters=output_clusters,
                collective_subtree_words=self._collective_subtrees[node.name],
                collective_subtree_words_per_root=self._collective_subtree_words_per_root,
                max_input_pending_size=self.router_pending_size,
                collective_buffer_size=getattr(self.arch, 'unified_interco_collective_buffer_size', 65536),
                collective_max_pending=getattr(self.arch, 'unified_interco_collective_max_pending', 1024),
                collective_alu_count=getattr(self.arch, 'unified_interco_collective_alu_count', self.link_width),
                collective_alu_latency=getattr(self.arch, 'unified_interco_collective_alu_latency', 1),
            )

        for node, port, link in self._router_to_link_bindings:
            node.component.o_OUTPUT(port, link.i_INPUT())
        for link, node, port in self._link_to_router_bindings:
            link.o_OUTPUT(node.component.i_INPUT(port))

    def _finalize_shortest_routes(self):
        self._compute_routes_shortest()
        self._instantiate_routers()

    def _finalize_spanning_tree_routes(self, root: RouterNode | None = None):
        self._compute_routes_spanning_tree(root)
        self._instantiate_routers()

    def _finalize_next_hop_routes(self, next_hop):
        self._compute_routes_from_next_hop(next_hop)
        self._instantiate_routers()

    def i_CLUSTER_INPUT(self, cluster_id: int) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, f'cluster_{cluster_id}_in', signature='io')

    def o_CLUSTER_OUTPUT(self, cluster_id: int, itf: gvsoc.systree.SlaveItf):
        self.itf_bind(f'cluster_{cluster_id}_out', itf, signature='io')
