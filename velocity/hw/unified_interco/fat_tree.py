import math
from dataclasses import dataclass, field

import gvsoc.systree

from pulp.chips.velocity.unified_interco.link import UnifiedLink
from pulp.chips.velocity.unified_interco.router import UnifiedRouter


@dataclass
class _RouterNode:
    name: str
    kind: str
    router_id: int
    routes: list[int]
    next_input: int = 0
    next_output: int = 0
    component: UnifiedRouter | None = None
    cluster_outputs: dict[int, int] = field(default_factory=dict)
    agg_outputs: dict[int, int] = field(default_factory=dict)
    edge_outputs: dict[int, int] = field(default_factory=dict)
    core_outputs: dict[int, int] = field(default_factory=dict)
    pod_outputs: dict[int, int] = field(default_factory=dict)


class FatTreeTopoHeart:
    def __init__(self, topology: 'FatTreeInterconnect'):
        self.topology = topology

    def attach(self):
        for pod in range(self.topology.pod_count):
            for edge in range(self.topology.edge_count):
                self._attach_edge(pod, edge)

            for agg in range(self.topology.agg_count):
                self._attach_aggregation(pod, agg)

        for group in range(self.topology.agg_count):
            for core in range(self.topology.core_count):
                self._attach_core(group, core)

    def _attach_edge(self, pod: int, edge: int):
        node = self.topology.edges[(pod, edge)]
        for cluster in range(self.topology.num_cluster):
            dst_pod, dst_edge, dst_host = self.topology.locate_cluster(cluster)
            if dst_pod == pod and dst_edge == edge:
                node.routes[cluster] = node.cluster_outputs[dst_host]
            else:
                node.routes[cluster] = node.agg_outputs[dst_pod % self.topology.agg_count]

    def _attach_aggregation(self, pod: int, agg: int):
        node = self.topology.aggs[(pod, agg)]
        for cluster in range(self.topology.num_cluster):
            dst_pod, dst_edge, _ = self.topology.locate_cluster(cluster)
            if dst_pod == pod:
                node.routes[cluster] = node.edge_outputs[dst_edge]
            else:
                node.routes[cluster] = node.core_outputs[dst_pod % self.topology.core_count]

    def _attach_core(self, group: int, core: int):
        node = self.topology.cores[(group, core)]
        for cluster in range(self.topology.num_cluster):
            dst_pod, _, _ = self.topology.locate_cluster(cluster)
            node.routes[cluster] = node.pod_outputs[dst_pod]


class FatTreeInterconnect(gvsoc.systree.Component):
    def __init__(self, parent, name, clusters, arch):
        super().__init__(parent, name)

        self.clusters = clusters
        self.num_cluster = arch.num_cluster
        self.cluster_stride = arch.dma_cluster_stride
        self.radix = getattr(arch, 'unified_interco_radix', 8)
        self.link_latency = getattr(arch, 'unified_interco_link_latency', 1)
        self.link_width = getattr(arch, 'unified_interco_link_width', arch.dma_bus_width)
        self.link_pending_size = getattr(arch, 'unified_interco_link_pending_size', arch.dma_write_buffer_size)
        self.router_pending_size = getattr(arch, 'unified_interco_router_pending_size', arch.dma_write_buffer_size)

        self._sanity_check_basic()

        self.host_count = self.radix // 2
        self.edge_count = self.radix // 2
        self.agg_count = self.radix // 2
        self.core_count = self.radix // 2
        self.cluster_per_pod = self.edge_count * self.host_count
        self.pod_count = math.ceil(self.num_cluster / self.cluster_per_pod)

        self._sanity_check_capacity()

        self.edges: dict[tuple[int, int], _RouterNode] = {}
        self.aggs: dict[tuple[int, int], _RouterNode] = {}
        self.cores: dict[tuple[int, int], _RouterNode] = {}
        self._links: list[UnifiedLink] = []
        self._router_to_link_bindings: list[tuple[_RouterNode, int, UnifiedLink]] = []
        self._link_to_router_bindings: list[tuple[UnifiedLink, _RouterNode, int]] = []

        self._create_nodes()
        self._connect_clusters()
        self._connect_edge_aggregation()
        self._connect_aggregation_core()
        FatTreeTopoHeart(self).attach()
        self._instantiate_routers()

    def locate_cluster(self, cluster: int) -> tuple[int, int, int]:
        pod = cluster // self.cluster_per_pod
        in_pod = cluster % self.cluster_per_pod
        edge = in_pod // self.host_count
        host = in_pod % self.host_count
        return pod, edge, host

    def _sanity_check_basic(self):
        if self.num_cluster <= 0:
            raise ValueError('Fat-tree unified interconnect requires num_cluster > 0')
        if self.cluster_stride <= 0:
            raise ValueError('Fat-tree unified interconnect requires dma_cluster_stride > 0')
        if self.radix < 4 or self.radix % 2 != 0:
            raise ValueError('Fat-tree unified interconnect requires an even radix >= 4')
        if self.link_latency < 0:
            raise ValueError('Fat-tree unified interconnect requires link latency >= 0')
        if self.link_width <= 0:
            raise ValueError('Fat-tree unified interconnect requires link width > 0')
        if self.link_pending_size < 0 or self.router_pending_size < 0:
            raise ValueError('Fat-tree unified interconnect pending sizes must be >= 0')

    def _sanity_check_capacity(self):
        max_clusters = (self.radix ** 3) // 4
        if self.num_cluster > max_clusters:
            raise ValueError(
                f'Fat-tree radix {self.radix} supports at most {max_clusters} clusters, '
                f'got {self.num_cluster}'
            )
        if self.pod_count > self.radix:
            raise ValueError(
                f'Fat-tree radix {self.radix} supports at most {self.radix} pods, '
                f'got {self.pod_count}'
            )

    def _new_node(self, collection, key, name, kind):
        router_id = len(self.edges) + len(self.aggs) + len(self.cores)
        collection[key] = _RouterNode(
            name=name,
            kind=kind,
            router_id=router_id,
            routes=[-1] * self.num_cluster,
        )

    def _create_nodes(self):
        for pod in range(self.pod_count):
            for edge in range(self.edge_count):
                self._new_node(self.edges, (pod, edge), f'edge_{pod}_{edge}', 'edge')
            for agg in range(self.agg_count):
                self._new_node(self.aggs, (pod, agg), f'agg_{pod}_{agg}', 'aggregation')

        for group in range(self.agg_count):
            for core in range(self.core_count):
                self._new_node(self.cores, (group, core), f'core_{group}_{core}', 'core')

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

    def _connect_clusters(self):
        for cluster_id, _ in enumerate(self.clusters):
            pod, edge, host = self.locate_cluster(cluster_id)
            edge_node = self.edges[(pod, edge)]

            in_port = self._alloc_input(edge_node)
            uplink = self._new_link(f'cluster_{cluster_id}_to_edge_{pod}_{edge}')
            self.bind(self, f'cluster_{cluster_id}_in', uplink, 'input')
            self._bind_link_to_router(uplink, edge_node, in_port)

            out_port = self._alloc_output(edge_node)
            edge_node.cluster_outputs[host] = out_port
            downlink = self._new_link(f'edge_{pod}_{edge}_to_cluster_{cluster_id}')
            self._bind_router_to_link(edge_node, out_port, downlink)
            downlink.o_OUTPUT(gvsoc.systree.SlaveItf(self, f'cluster_{cluster_id}_out', signature='io'))

    def _connect_edge_aggregation(self):
        for pod in range(self.pod_count):
            for edge in range(self.edge_count):
                edge_node = self.edges[(pod, edge)]
                for agg in range(self.agg_count):
                    agg_node = self.aggs[(pod, agg)]

                    edge_out = self._alloc_output(edge_node)
                    agg_in = self._alloc_input(agg_node)
                    edge_node.agg_outputs[agg] = edge_out
                    link_up = self._new_link(f'edge_{pod}_{edge}_to_agg_{pod}_{agg}')
                    self._bind_router_to_link(edge_node, edge_out, link_up)
                    self._bind_link_to_router(link_up, agg_node, agg_in)

                    agg_out = self._alloc_output(agg_node)
                    edge_in = self._alloc_input(edge_node)
                    agg_node.edge_outputs[edge] = agg_out
                    link_down = self._new_link(f'agg_{pod}_{agg}_to_edge_{pod}_{edge}')
                    self._bind_router_to_link(agg_node, agg_out, link_down)
                    self._bind_link_to_router(link_down, edge_node, edge_in)

    def _connect_aggregation_core(self):
        for pod in range(self.pod_count):
            for agg in range(self.agg_count):
                agg_node = self.aggs[(pod, agg)]
                for core in range(self.core_count):
                    core_node = self.cores[(agg, core)]

                    agg_out = self._alloc_output(agg_node)
                    core_in = self._alloc_input(core_node)
                    agg_node.core_outputs[core] = agg_out
                    link_up = self._new_link(f'agg_{pod}_{agg}_to_core_{agg}_{core}')
                    self._bind_router_to_link(agg_node, agg_out, link_up)
                    self._bind_link_to_router(link_up, core_node, core_in)

                    core_out = self._alloc_output(core_node)
                    agg_in = self._alloc_input(agg_node)
                    core_node.pod_outputs[pod] = core_out
                    link_down = self._new_link(f'core_{agg}_{core}_to_agg_{pod}_{agg}')
                    self._bind_router_to_link(core_node, core_out, link_down)
                    self._bind_link_to_router(link_down, agg_node, agg_in)

    def _bind_router_to_link(self, node: _RouterNode, port: int, link: UnifiedLink):
        self._router_to_link_bindings.append((node, port, link))

    def _bind_link_to_router(self, link: UnifiedLink, node: _RouterNode, port: int):
        self._link_to_router_bindings.append((link, node, port))

    def _instantiate_routers(self):
        for node in list(self.edges.values()) + list(self.aggs.values()) + list(self.cores.values()):
            node.component = UnifiedRouter(
                self,
                node.name,
                router_id=node.router_id,
                radix=self.radix,
                num_cluster=self.num_cluster,
                cluster_stride=self.cluster_stride,
                routes=node.routes,
                max_input_pending_size=self.router_pending_size,
            )

        for node, port, link in self._router_to_link_bindings:
            node.component.o_OUTPUT(port, link.i_INPUT())
        for link, node, port in self._link_to_router_bindings:
            link.o_OUTPUT(node.component.i_INPUT(port))

    def i_CLUSTER_INPUT(self, cluster_id: int) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, f'cluster_{cluster_id}_in', signature='io')

    def o_CLUSTER_OUTPUT(self, cluster_id: int, itf: gvsoc.systree.SlaveItf):
        self.itf_bind(f'cluster_{cluster_id}_out', itf, signature='io')
