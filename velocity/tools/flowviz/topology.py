from __future__ import annotations

import math
import re
from dataclasses import asdict
from pathlib import Path
from typing import Any

from .arch_loader import load_arch
from .model import ArchInfo, FlowEvent, Link, Node, PacketInfo, Segment, int_field
from .trace_parser import parse_trace


CLUSTER_TO_NODE_RE = re.compile(r"^cluster_(?P<cluster>\d+)_to_(?P<router>.+)$")
NODE_TO_CLUSTER_RE = re.compile(r"^(?P<router>.+)_to_cluster_(?P<cluster>\d+)$")
NODE_TO_NODE_RE = re.compile(r"^(?P<src>.+)_to_(?P<dst>.+)$")


class FlowModelBuilder:
    def __init__(self, arch: ArchInfo, events: list[FlowEvent], system_info: dict[str, int]):
        self.arch = arch
        self.events = events
        self.system_info = system_info
        if "num_chip" in system_info:
            self.arch.trace_num_chip = system_info["num_chip"]
            self.arch.num_chip = system_info["num_chip"]
        if "num_cluster" in system_info:
            self.arch.trace_num_cluster = system_info["num_cluster"]
            self.arch.num_cluster = system_info["num_cluster"]

        self.nodes: dict[str, Node] = {}
        self.links: dict[str, Link] = {}
        self.packets: dict[str, PacketInfo] = {}
        self.segments: list[Segment] = []

    def build(self) -> dict[str, Any]:
        self._add_chips_and_clusters()
        self._add_expected_topologies()
        self._add_observed_components()
        self._add_packets()
        self._add_segments()
        cycles = [event.cycle for event in self.events]
        return {
            "arch": {
                "path": self.arch.path,
                "num_chip": self.arch.num_chip,
                "num_cluster": self.arch.num_cluster,
                "onchip": self.arch.onchip,
                "offchip": self.arch.offchip,
                "trace_num_chip": self.arch.trace_num_chip,
                "trace_num_cluster": self.arch.trace_num_cluster,
            },
            "time": {
                "start": min(cycles) if cycles else 0,
                "end": max(cycles) if cycles else 1,
                "event_count": len(self.events),
            },
            "nodes": [asdict(node) for node in self.nodes.values()],
            "links": [asdict(link) for link in self.links.values()],
            "packets": [asdict(packet) for packet in self.packets.values()],
            "segments": [asdict(segment) for segment in self.segments],
            "events": [self._event_to_dict(event) for event in self.events],
        }

    def _add_node(self, node: Node):
        self.nodes.setdefault(node.id, node)

    def _add_link(self, link: Link):
        if link.source in self.nodes and link.target in self.nodes:
            self.links.setdefault(link.id, link)

    def _cluster_node_id(self, chip: int, cluster: int) -> str:
        return f"cluster:{chip}:{cluster}"

    def _router_node_id(self, scope: str, chip: int | None, router: str) -> str:
        if scope == "onchip":
            return f"router:chip:{chip}:{router}"
        return f"router:offchip:{router}"

    def _add_chips_and_clusters(self):
        for chip in range(max(self.arch.num_chip, 1)):
            self._add_node(Node(
                id=f"chip:{chip}",
                kind="chip",
                label=f"Chip {chip}",
                chip=chip,
                scope="chip",
            ))
            for cluster in range(max(self.arch.num_cluster, 1)):
                label = f"C{cluster}" if cluster != 0 else "C0 / RDMA"
                self._add_node(Node(
                    id=self._cluster_node_id(chip, cluster),
                    kind="cluster",
                    label=label,
                    chip=chip,
                    cluster=cluster,
                    scope="chip",
                ))

    def _add_observed_components(self):
        for event in self.events:
            if event.component == "router":
                router = self._router_name_from_component(event.component_name)
                self._add_router(event.scope, event.chip, router)
            elif event.component == "link":
                self._add_link_from_event(event)
            elif event.component == "dma":
                self._add_dma_endpoint(event)

    def _add_router(self, scope: str, chip: int | None, router: str):
        node_id = self._router_node_id(scope, chip, router)
        label = self._router_label(router)
        self._add_node(Node(
            id=node_id,
            kind="router",
            label=label,
            chip=chip,
            scope=scope,
            router=router,
        ))

    def _add_dma_endpoint(self, event: FlowEvent):
        node = self._node_for_dma_event(event)
        if node is not None and node not in self.nodes:
            chip = int(node.split(":")[1])
            cluster = int(node.split(":")[2])
            self._add_node(Node(
                id=node,
                kind="cluster",
                label=f"C{cluster}",
                chip=chip,
                cluster=cluster,
                scope="chip",
            ))

    def _add_link_from_event(self, event: FlowEvent):
        link = self._link_for_component(event)
        if link is not None:
            self._add_link(link)

    def _link_for_component(self, event: FlowEvent) -> Link | None:
        name = event.component_name
        scope = event.scope
        chip = event.chip

        match = CLUSTER_TO_NODE_RE.match(name)
        if match:
            cluster = int(match.group("cluster"))
            router = self._router_name_from_component(match.group("router"))
            source = self._endpoint_cluster(scope, chip, cluster)
            target = self._router_node_id(scope, chip, router)
            self._add_router(scope, chip, router)
            return Link(event.component_path, source, target, name, scope, chip)

        match = NODE_TO_CLUSTER_RE.match(name)
        if match:
            cluster = int(match.group("cluster"))
            router = self._router_name_from_component(match.group("router"))
            source = self._router_node_id(scope, chip, router)
            target = self._endpoint_cluster(scope, chip, cluster)
            self._add_router(scope, chip, router)
            return Link(event.component_path, source, target, name, scope, chip)

        match = NODE_TO_NODE_RE.match(name)
        if match:
            src_router = self._router_name_from_component(match.group("src"))
            dst_router = self._router_name_from_component(match.group("dst"))
            if src_router.startswith("cluster_") or dst_router.startswith("cluster_"):
                return None
            source = self._router_node_id(scope, chip, src_router)
            target = self._router_node_id(scope, chip, dst_router)
            self._add_router(scope, chip, src_router)
            self._add_router(scope, chip, dst_router)
            return Link(event.component_path, source, target, name, scope, chip)

        return None

    def _endpoint_cluster(self, scope: str, chip: int | None, cluster: int) -> str:
        if scope == "offchip":
            return self._cluster_node_id(cluster, 0)
        return self._cluster_node_id(chip or 0, cluster)

    def _router_name_from_component(self, component_name: str) -> str:
        if component_name.startswith("router_"):
            return component_name[len("router_"):]
        return component_name

    def _router_component_name(self, router: str) -> str:
        if router == "root" or router.startswith("level_"):
            return router
        return f"router_{router}"

    def _router_label(self, router: str) -> str:
        if router == "root":
            return "Root"
        if router.startswith("level_"):
            parts = router.split("_")
            if len(parts) >= 3 and parts[1].isdigit():
                return f"L{parts[1]}:{'.'.join(parts[2:])}"
        if router.replace("_", "").isdigit():
            return f"R{router}"
        return router

    def _add_expected_topologies(self):
        if self._topology_enabled(self.arch.onchip):
            for chip in range(max(self.arch.num_chip, 1)):
                self._add_expected_topology("onchip", chip, max(self.arch.num_cluster, 1))

        if self.arch.num_chip > 1 and self._topology_enabled(self.arch.offchip):
            self._add_expected_topology("offchip", None, self.arch.num_chip)

    def _topology_enabled(self, topology: Any) -> bool:
        if topology is None or topology is False:
            return False
        if isinstance(topology, str):
            return topology.lower().replace("-", "_") not in ("", "none", "legacy", "flat")
        return True

    def _add_expected_topology(self, scope: str, chip: int | None, endpoint_count: int):
        topology = self.arch.onchip if scope == "onchip" else self.arch.offchip
        topology = self._normalize_topology_name(topology)

        if topology == "fat_tree":
            self._build_expected_fat_tree(scope, chip, endpoint_count)
        elif topology == "tree":
            self._build_expected_tree(scope, chip, endpoint_count)
        elif topology == "ring":
            self._build_expected_ring(scope, chip, endpoint_count)
        elif topology == "hypercube":
            self._build_expected_hypercube(scope, chip, endpoint_count)
        elif topology == "dragonfly":
            self._build_expected_dragonfly(scope, chip, endpoint_count)
        elif topology in (
            "mesh_2d", "mesh_3d",
            "torus_2d", "torus_3d",
            "ruche_2d", "ruche_3d",
            "hexa_mesh", "hexa_torus",
            "octa_mesh", "octa_torus",
        ):
            self._build_expected_coordinate(scope, chip, endpoint_count, topology)

    def _normalize_topology_name(self, topology: Any) -> str:
        name = str(topology).lower().replace("-", "_").replace(" ", "_")
        aliases = {
            "fattree": "fat_tree",
            "2d_mesh": "mesh_2d",
            "3d_mesh": "mesh_3d",
            "2d_torus": "torus_2d",
            "3d_torus": "torus_3d",
            "h_hop_2d_ruche_mesh": "ruche_2d",
            "h_hop_3d_ruche_mesh": "ruche_3d",
            "hexamesh": "hexa_mesh",
            "hexatorus": "hexa_torus",
            "octamesh": "octa_mesh",
            "octatorus": "octa_torus",
        }
        return aliases.get(name, name)

    def _add_topology_router(self, scope: str, chip: int | None, router_name: str) -> str:
        router = self._router_name_from_component(router_name)
        self._add_router(scope, chip, router)
        return self._router_node_id(scope, chip, router)

    def _add_topology_link(self, scope: str, chip: int | None, name: str, source: str, target: str):
        component_path = self._component_path(scope, chip, name)
        self._add_link(Link(component_path, source, target, name, scope, chip))

    def _connect_endpoint(self, scope: str, chip: int | None, endpoint: int, router_name: str):
        router = self._router_name_from_component(router_name)
        source = self._endpoint_cluster(scope, chip, endpoint)
        target = self._add_topology_router(scope, chip, router)
        router_component = self._router_component_name(router)
        self._add_topology_link(scope, chip, f"cluster_{endpoint}_to_{router_component}", source, target)
        self._add_topology_link(scope, chip, f"{router_component}_to_cluster_{endpoint}", target, source)

    def _connect_router_pair(self, scope: str, chip: int | None, left_name: str, right_name: str):
        left = self._router_name_from_component(left_name)
        right = self._router_name_from_component(right_name)
        left_node = self._add_topology_router(scope, chip, left)
        right_node = self._add_topology_router(scope, chip, right)
        left_component = self._router_component_name(left)
        right_component = self._router_component_name(right)
        self._add_topology_link(scope, chip, f"{left_component}_to_{right_component}", left_node, right_node)
        self._add_topology_link(scope, chip, f"{right_component}_to_{left_component}", right_node, left_node)

    def _component_path(self, scope: str, chip: int | None, name: str) -> str:
        if scope == "onchip":
            return f"chip_{chip}/onchip_interco/{name}"
        return f"offchip_interco/{name}"

    def _scope_attr(self, scope: str, suffix: str, default: Any = None) -> Any:
        names = [f"{scope}_{suffix}"]
        if suffix.startswith("tree_"):
            names.append(f"{scope}_{suffix.removeprefix('tree_')}")
        names.append(f"unified_interco_{suffix}")
        for name in names:
            if name in self.arch.attrs:
                return self.arch.attrs[name]
        return default

    def _int_attr(self, scope: str, suffix: str, default: int) -> int:
        value = self._scope_attr(scope, suffix, default)
        if isinstance(value, bool):
            return default
        try:
            return int(value)
        except (TypeError, ValueError):
            return default

    def _dims_attr(self, scope: str, topology: str, ndim: int, endpoint_count: int) -> tuple[int, ...]:
        key = self._topology_attr_key(topology)
        family = key.rsplit("_", 1)[0] if key.endswith(("_2d", "_3d")) else key
        candidates = [
            f"{scope}_{key}_dims",
            f"{scope}_{family}_dims",
            f"{scope}_dims",
            f"unified_interco_{key}_dims",
            f"unified_interco_{family}_dims",
            "unified_interco_dims",
        ]
        for name in candidates:
            value = self.arch.attrs.get(name)
            if isinstance(value, (list, tuple)) and len(value) == ndim:
                dims = tuple(int(item) for item in value)
                if all(dim > 0 for dim in dims):
                    return dims
        return self._infer_dims(endpoint_count, ndim)

    def _topology_attr_key(self, topology: str) -> str:
        name = self._normalize_topology_name(topology)
        if name.startswith("2d_"):
            return name[3:] + "_2d"
        if name.startswith("3d_"):
            return name[3:] + "_3d"
        if name.startswith("h_hop_"):
            return name[6:]
        return name

    def _infer_dims(self, endpoint_count: int, ndim: int) -> tuple[int, ...]:
        dims = [1] * ndim
        while math.prod(dims) < endpoint_count:
            index = min(range(ndim), key=lambda dim: dims[dim])
            dims[index] += 1
        return tuple(dims)

    def _index_to_coord(self, index: int, dims: tuple[int, ...]) -> tuple[int, ...]:
        coord = []
        for dim in dims:
            coord.append(index % dim)
            index //= dim
        return tuple(coord)

    def _iter_coords(self, dims: tuple[int, ...]):
        for index in range(math.prod(dims)):
            yield self._index_to_coord(index, dims)

    def _coord_router_name(self, coord: tuple[int, ...]) -> str:
        return "router_" + "_".join(str(value) for value in coord)

    def _build_expected_ring(self, scope: str, chip: int | None, endpoint_count: int):
        size = max(endpoint_count, self._int_attr(scope, "ring_size", endpoint_count))
        routers = [f"router_{index}" for index in range(size)]
        for router in routers:
            self._add_topology_router(scope, chip, router)
        for endpoint in range(endpoint_count):
            self._connect_endpoint(scope, chip, endpoint, routers[endpoint])
        if size <= 1:
            return
        for index in range(size - 1):
            self._connect_router_pair(scope, chip, routers[index], routers[index + 1])
        self._connect_router_pair(scope, chip, routers[-1], routers[0])

    def _build_expected_hypercube(self, scope: str, chip: int | None, endpoint_count: int):
        default_dims = max(1, math.ceil(math.log2(max(endpoint_count, 1))))
        ndim = self._int_attr(scope, "hypercube_dims", default_dims)
        capacity = max(endpoint_count, 1 << max(ndim, 0))
        routers = [f"router_{index}" for index in range(capacity)]
        for router in routers:
            self._add_topology_router(scope, chip, router)
        for endpoint in range(endpoint_count):
            self._connect_endpoint(scope, chip, endpoint, routers[endpoint])
        for index in range(capacity):
            for bit in range(ndim):
                peer = index ^ (1 << bit)
                if index < peer and peer < capacity:
                    self._connect_router_pair(scope, chip, routers[index], routers[peer])

    def _build_expected_tree(self, scope: str, chip: int | None, endpoint_count: int):
        radix = max(1, self._int_attr(scope, "tree_radix", 2))
        level = max(1, self._int_attr(scope, "tree_level", 1))
        paths: list[tuple[int, ...]] = []
        for depth in range(level):
            for index in range(radix ** depth):
                paths.append(self._path_from_index(index, depth, radix))
        for path in paths:
            self._add_topology_router(scope, chip, self._tree_node_name(path))
        for depth in range(1, level):
            for index in range(radix ** depth):
                path = self._path_from_index(index, depth, radix)
                self._connect_router_pair(
                    scope,
                    chip,
                    self._tree_node_name(path[:-1]),
                    self._tree_node_name(path),
                )
        for endpoint in range(endpoint_count):
            leaf_digits = self._path_from_index(endpoint, level, radix)
            self._connect_endpoint(scope, chip, endpoint, self._tree_node_name(leaf_digits[:-1]))

    def _path_from_index(self, index: int, length: int, radix: int) -> tuple[int, ...]:
        digits = [0] * length
        for pos in range(length - 1, -1, -1):
            digits[pos] = index % radix
            index //= radix
        return tuple(digits)

    def _tree_node_name(self, path: tuple[int, ...]) -> str:
        if not path:
            return "root"
        return "level_" + str(len(path)) + "_" + "_".join(str(value) for value in path)

    def _build_expected_fat_tree(self, scope: str, chip: int | None, endpoint_count: int):
        radix = max(1, self._int_attr(scope, "tree_radix", 8))
        level = max(1, self._int_attr(scope, "tree_level", 3))
        if level == 1:
            root = "level_0_0"
            self._add_topology_router(scope, chip, root)
            for endpoint in range(endpoint_count):
                self._connect_endpoint(scope, chip, endpoint, root)
            return

        down_ports = (radix + 1) // 2
        up_ports = radix - down_ports
        nodes: set[tuple[int, tuple[int, ...]]] = set()
        links: set[tuple[str, str]] = set()

        def node_name(node_level: int, coord: tuple[int, ...]) -> str:
            coord_name = "_".join(str(value) for value in coord) if coord else "0"
            return f"level_{node_level}_{coord_name}"

        def locate_cluster(cluster: int) -> tuple[int, tuple[int, ...]]:
            local_capacity = down_ports ** (level - 1)
            pod = cluster // local_capacity
            local = cluster % local_capacity
            digits = []
            for power in range(level - 2, -1, -1):
                divisor = down_ports ** power
                digits.append(local // divisor)
                local %= divisor
            return pod, tuple(digits)

        def leaf_coord(cluster_coord: tuple[int, tuple[int, ...]]) -> tuple[int, ...]:
            pod, digits = cluster_coord
            return (pod,) + digits[:-1]

        def upper_coord(lower_level: int, lower_coord: tuple[int, ...], up_index: int) -> tuple[int, ...]:
            lower_down_count = level - 2 - lower_level
            upper_down_count = level - 2 - (lower_level + 1)
            up_prefix = lower_coord[1 + lower_down_count:]
            if lower_level + 1 == level - 1:
                return up_prefix + (up_index,)
            pod = lower_coord[0]
            down_prefix = lower_coord[1:1 + upper_down_count]
            return (pod,) + down_prefix + up_prefix + (up_index,)

        for endpoint in range(endpoint_count):
            coord = leaf_coord(locate_cluster(endpoint))
            nodes.add((0, coord))
            self._connect_endpoint(scope, chip, endpoint, node_name(0, coord))

        for lower_level in range(level - 1):
            lower_coords = sorted(coord for node_level, coord in nodes if node_level == lower_level)
            for lower_coord in lower_coords:
                lower = (lower_level, lower_coord)
                for up_index in range(up_ports):
                    upper = (lower_level + 1, upper_coord(lower_level, lower_coord, up_index))
                    nodes.add(upper)
                    links.add((node_name(*lower), node_name(*upper)))

        for node_level, coord in sorted(nodes):
            self._add_topology_router(scope, chip, node_name(node_level, coord))
        for lower, upper in sorted(links):
            self._connect_router_pair(scope, chip, lower, upper)

    def _build_expected_coordinate(self, scope: str, chip: int | None, endpoint_count: int, topology: str):
        topology = self._topology_attr_key(topology)
        ndim = 3 if topology.endswith("_3d") else 2
        dims = self._dims_attr(scope, topology, ndim, endpoint_count)
        wrap = "torus" in topology
        extra_offsets: list[tuple[int, ...]] = []
        if "ruche" in topology:
            hop = max(2, self._int_attr(scope, f"{topology}_hop", self._int_attr(scope, "ruche_hop", 2)))
            extra_offsets = [tuple(hop if axis == dim else 0 for axis in range(ndim)) for dim in range(ndim)]
        elif topology.startswith("hexa"):
            extra_offsets = [(1, -1)]
        elif topology.startswith("octa"):
            extra_offsets = [(1, -1), (1, 1)]

        routers = {coord: self._coord_router_name(coord) for coord in self._iter_coords(dims)}
        for router in routers.values():
            self._add_topology_router(scope, chip, router)
        for endpoint in range(endpoint_count):
            self._connect_endpoint(scope, chip, endpoint, routers[self._index_to_coord(endpoint, dims)])

        unit_offsets = []
        for axis in range(ndim):
            offset = [0] * ndim
            offset[axis] = 1
            unit_offsets.append(tuple(offset))
        for offset in unit_offsets + extra_offsets:
            self._connect_coordinate_offset(scope, chip, dims, routers, offset, wrap)

    def _connect_coordinate_offset(
        self,
        scope: str,
        chip: int | None,
        dims: tuple[int, ...],
        routers: dict[tuple[int, ...], str],
        offset: tuple[int, ...],
        wrap: bool,
    ):
        for coord in self._iter_coords(dims):
            dst = []
            valid = True
            for value, step, dim in zip(coord, offset, dims):
                next_value = value + step
                if wrap:
                    next_value %= dim
                elif next_value < 0 or next_value >= dim:
                    valid = False
                    break
                dst.append(next_value)
            if valid:
                self._connect_router_pair(scope, chip, routers[coord], routers[tuple(dst)])

    def _build_expected_dragonfly(self, scope: str, chip: int | None, endpoint_count: int):
        default_groups = max(1, math.ceil(math.sqrt(endpoint_count)))
        default_routers = max(1, math.ceil(endpoint_count / default_groups))
        groups = self._int_attr(scope, "dragonfly_groups", default_groups)
        routers_per_group = self._int_attr(scope, "dragonfly_routers_per_group", default_routers)
        terminals_per_router = self._int_attr(scope, "dragonfly_terminals_per_router", 1)

        routers: dict[tuple[int, int], str] = {}
        for group in range(groups):
            for router in range(routers_per_group):
                name = f"router_{group}_{router}"
                routers[(group, router)] = name
                self._add_topology_router(scope, chip, name)

        for endpoint in range(endpoint_count):
            router_index = endpoint // terminals_per_router
            group = router_index // routers_per_group
            router = router_index % routers_per_group
            if (group, router) in routers:
                self._connect_endpoint(scope, chip, endpoint, routers[(group, router)])

        for group in range(groups):
            for left in range(routers_per_group):
                for right in range(left + 1, routers_per_group):
                    self._connect_router_pair(scope, chip, routers[(group, left)], routers[(group, right)])

        for router in range(routers_per_group):
            for left_group in range(groups):
                for right_group in range(left_group + 1, groups):
                    self._connect_router_pair(scope, chip, routers[(left_group, router)], routers[(right_group, router)])

    def _node_for_dma_event(self, event: FlowEvent) -> str | None:
        if event.scope != "chip" or event.chip is None:
            return None
        node = int_field(event.fields.get("node"), 0)
        if event.component_name == "rdma":
            return self._cluster_node_id(node, 0)
        return self._cluster_node_id(event.chip, node)

    def _add_packets(self):
        for event in self.events:
            packet_id = event.packet_id
            if packet_id == "unknown":
                continue
            if packet_id not in self.packets:
                self.packets[packet_id] = PacketInfo(id=packet_id)
            packet = self.packets[packet_id]
            fields = event.fields
            packet.kind = fields.get("kind", packet.kind)
            packet.action = fields.get("action", packet.action)
            packet.phase = fields.get("phase", packet.phase)
            packet.op = fields.get("op", packet.op)
            packet.src = fields.get("src", packet.src)
            packet.dst = fields.get("dst", packet.dst)
            packet.root = fields.get("root", packet.root)
            packet.size = max(packet.size, int_field(fields.get("size"), 0))
            packet.bytes = max(packet.bytes, int_field(fields.get("bytes"), 0))

    def _add_segments(self):
        open_segments: dict[tuple[str, str, str, str], list[FlowEvent]] = {}

        for event in self.events:
            if event.packet_id == "unknown":
                continue

            if event.component == "dma":
                self._add_dma_segment(event)
                continue

            if event.event == "enter":
                key = self._open_key(event)
                open_segments.setdefault(key, []).append(event)
                continue

            if event.event != "exit":
                continue

            start = self._pop_open_event(open_segments, event)
            if start is None:
                continue
            self._add_component_segment(start, event)

    def _open_key(self, event: FlowEvent) -> tuple[str, str, str, str]:
        return (event.packet_id, event.component_path, event.component, event.direction)

    def _pop_open_event(
        self,
        open_segments: dict[tuple[str, str, str, str], list[FlowEvent]],
        event: FlowEvent,
    ) -> FlowEvent | None:
        candidates = [
            self._open_key(event),
            (event.packet_id, event.component_path, event.component, "generated"),
            (event.packet_id, event.component_path, event.component, "req"),
            (event.packet_id, event.component_path, event.component, "resp"),
        ]
        for key in candidates:
            queue = open_segments.get(key)
            if queue:
                return queue.pop(0)
        return None

    def _add_dma_segment(self, event: FlowEvent):
        if event.event != "enter":
            return
        node = self._node_for_dma_event(event)
        if node is None:
            return
        duration = 1
        if event.direction == "rx":
            exit_event = self._find_matching_dma_exit(event)
            if exit_event is not None:
                duration = max(exit_event.cycle - event.cycle, 1)
        self.segments.append(Segment(
            id=f"seg:{len(self.segments)}",
            packet_id=event.packet_id,
            kind="node",
            component="dma",
            component_path=event.component_path,
            direction=event.direction,
            start_cycle=event.cycle,
            end_cycle=event.cycle + duration,
            node=node,
            status=event.fields.get("status", ""),
            event_index=event.index,
        ))

    def _find_matching_dma_exit(self, enter_event: FlowEvent) -> FlowEvent | None:
        for event in self.events[enter_event.index + 1:]:
            if event.packet_id != enter_event.packet_id:
                continue
            if event.component_path != enter_event.component_path:
                continue
            if event.component != "dma" or event.direction != enter_event.direction:
                continue
            if event.event == "exit":
                return event
        return None

    def _add_component_segment(self, start: FlowEvent, end: FlowEvent):
        if start.component == "link":
            link = self.links.get(start.component_path)
            if link is None:
                return
            source = link.source
            target = link.target
            if start.direction == "resp":
                source, target = target, source
            self.segments.append(Segment(
                id=f"seg:{len(self.segments)}",
                packet_id=start.packet_id,
                kind="link",
                component="link",
                component_path=start.component_path,
                direction=start.direction,
                start_cycle=start.cycle,
                end_cycle=max(end.cycle, start.cycle + 1),
                source=source,
                target=target,
                status=end.fields.get("status", start.fields.get("status", "")),
                event_index=start.index,
            ))
            return

        if start.component == "router":
            node = self._router_node_for_event(start)
            if node is None:
                return
            self.segments.append(Segment(
                id=f"seg:{len(self.segments)}",
                packet_id=start.packet_id,
                kind="node",
                component="router",
                component_path=start.component_path,
                direction=start.direction,
                start_cycle=start.cycle,
                end_cycle=max(end.cycle, start.cycle + 1),
                node=node,
                status=end.fields.get("status", start.fields.get("status", "")),
                event_index=start.index,
            ))

    def _router_node_for_event(self, event: FlowEvent) -> str | None:
        router = self._router_name_from_component(event.component_name)
        return self._router_node_id(event.scope, event.chip, router)

    def _event_to_dict(self, event: FlowEvent) -> dict[str, Any]:
        return {
            "index": event.index,
            "time_ps": event.time_ps,
            "cycle": event.cycle,
            "path": event.path,
            "component_path": event.component_path,
            "scope": event.scope,
            "chip": event.chip,
            "component_name": event.component_name,
            "fields": event.fields,
        }


def build_flow_model(arch_path: str | Path, trace_path: str | Path) -> dict[str, Any]:
    arch = load_arch(arch_path)
    events, system_info = parse_trace(trace_path)
    return FlowModelBuilder(arch, events, system_info).build()
