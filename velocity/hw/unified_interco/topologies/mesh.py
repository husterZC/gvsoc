from pulp.chips.velocity.unified_interco.topologies.base import GraphTopologyInterconnect
from pulp.chips.velocity.unified_interco.topologies.utils import (
    coord_name,
    get_dims,
    get_int_attr,
    index_to_coord,
    iter_coords,
    product,
)


class CoordinateInterconnect(GraphTopologyInterconnect):
    def __init__(
        self,
        parent,
        name,
        clusters,
        arch,
        topology_name: str,
        dims: tuple[int, ...],
        wrap: bool,
        extra_offsets: list[tuple[int, ...]] | None = None,
        ruche_hop: int | None = None,
        diagonal_mode: str | None = None,
    ):
        self.dims = dims
        self.wrap = wrap
        self.extra_offsets = extra_offsets or []
        self.ruche_hop = ruche_hop
        self.diagonal_mode = diagonal_mode
        self.nodes_by_coord = {}
        self.routing_mode = _routing_mode(arch, topology_name, wrap)

        super().__init__(parent, name, clusters, arch, topology_name)
        self._check_capacity(product(self.dims))
        self._build()
        if self.routing_mode in ('wrap_tree', 'tree'):
            self._finalize_spanning_tree_routes(self.nodes_by_coord[tuple(0 for _ in self.dims)])
        else:
            self._finalize_next_hop_routes(self._dimension_order_next_hop)

    def _build(self):
        for coord in iter_coords(self.dims):
            self.nodes_by_coord[coord] = self._new_node(coord_name('router', coord), coord=coord)

        for cluster_id in range(self.num_cluster):
            self._connect_cluster(cluster_id, self.nodes_by_coord[index_to_coord(cluster_id, self.dims)])

        unit_offsets = []
        for axis in range(len(self.dims)):
            offset = [0] * len(self.dims)
            offset[axis] = 1
            unit_offsets.append(tuple(offset))

        for offset in unit_offsets + self.extra_offsets:
            self._connect_offset(offset)

    def _connect_offset(self, offset: tuple[int, ...]):
        for coord in iter_coords(self.dims):
            dst = []
            valid = True
            for value, step, dim in zip(coord, offset, self.dims):
                next_value = value + step
                if self.wrap:
                    next_value %= dim
                elif next_value < 0 or next_value >= dim:
                    valid = False
                    break
                dst.append(next_value)

            if valid:
                self._connect_router_pair(self.nodes_by_coord[coord], self.nodes_by_coord[tuple(dst)])

    def _dimension_order_next_hop(self, node, cluster_id: int, target):
        coord = node.coord
        dst = target.coord

        diagonal = self._diagonal_next_coord(coord, dst)
        if diagonal is not None:
            return self.nodes_by_coord[diagonal]

        for axis in range(len(self.dims)):
            if coord[axis] == dst[axis]:
                continue

            step = self._axis_step(coord, dst, axis)
            next_coord = list(coord)
            next_coord[axis] += step
            if self.wrap:
                next_coord[axis] %= self.dims[axis]
            return self.nodes_by_coord[tuple(next_coord)]

        raise ValueError(f'{self.topology_name} router {node.name} is already at destination {cluster_id}')

    def _axis_step(self, coord: tuple[int, ...], dst: tuple[int, ...], axis: int) -> int:
        if self.wrap and self.routing_mode in ('dimension_order', 'minimal', 'wrap_minimal'):
            dim = self.dims[axis]
            forward = (dst[axis] - coord[axis]) % dim
            backward = (coord[axis] - dst[axis]) % dim
            if forward == 0:
                return 0
            if forward <= backward:
                return 1
            return -1

        delta = dst[axis] - coord[axis]
        direction = 1 if delta > 0 else -1

        if self.ruche_hop is not None and abs(delta) >= self.ruche_hop:
            return direction * self.ruche_hop
        return direction

    def _diagonal_next_coord(self, coord: tuple[int, ...], dst: tuple[int, ...]) -> tuple[int, ...] | None:
        if len(self.dims) != 2 or self.diagonal_mode is None:
            return None

        if self.wrap:
            dx = self._wrapped_delta(coord, dst, 0)
            dy = self._wrapped_delta(coord, dst, 1)
        else:
            dx = dst[0] - coord[0]
            dy = dst[1] - coord[1]
        if dx == 0 or dy == 0:
            return None

        sx = 1 if dx > 0 else -1
        sy = 1 if dy > 0 else -1

        if self.diagonal_mode == 'hexa' and sx == sy:
            return None
        if self.diagonal_mode not in ('hexa', 'octa'):
            return None

        if self.wrap:
            next_coord = ((coord[0] + sx) % self.dims[0], (coord[1] + sy) % self.dims[1])
        else:
            next_coord = (coord[0] + sx, coord[1] + sy)
        if next_coord in self.nodes_by_coord and (
            coord_name('router', coord),
            coord_name('router', next_coord),
        ) in self._edge_ports:
            return next_coord
        return None

    def _wrapped_delta(self, coord: tuple[int, ...], dst: tuple[int, ...], axis: int) -> int:
        dim = self.dims[axis]
        forward = (dst[axis] - coord[axis]) % dim
        backward = (coord[axis] - dst[axis]) % dim
        if forward == 0:
            return 0
        if forward <= backward:
            return forward
        return -backward


def _ruche_hop(arch, topology_name: str) -> int:
    hop = get_int_attr(
        arch,
        [
            f'unified_interco_{topology_name}_hop',
            'unified_interco_ruche_hop',
            'unified_interco_hop',
        ],
        default=2,
    )
    if hop < 2:
        raise ValueError(f'{topology_name} requires H-hop distance >= 2')
    return hop


def _routing_mode(arch, topology_name: str, wrap: bool) -> str:
    mode = getattr(
        arch,
        f'unified_interco_{topology_name}_routing',
        getattr(arch, 'unified_interco_routing', 'wrap_tree' if wrap else 'dimension_order'),
    )
    if not isinstance(mode, str):
        raise ValueError(f'{topology_name} routing mode must be a string')
    mode = mode.lower().replace('-', '_')
    allowed = {'dimension_order', 'minimal', 'wrap_minimal', 'wrap_tree', 'tree'}
    if mode not in allowed:
        raise ValueError(f'{topology_name} unsupported routing mode: {mode}')
    return mode


def _check_ruche_dims(dims: tuple[int, ...], hop: int, topology_name: str):
    if all(dim < hop for dim in dims):
        raise ValueError(f'{topology_name} requires at least one dimension >= H={hop}')


class Mesh2DInterconnect(CoordinateInterconnect):
    def __init__(self, parent, name, clusters, arch):
        super().__init__(parent, name, clusters, arch, 'mesh_2d', get_dims(arch, 'mesh_2d', 2), wrap=False)


class Mesh3DInterconnect(CoordinateInterconnect):
    def __init__(self, parent, name, clusters, arch):
        super().__init__(parent, name, clusters, arch, 'mesh_3d', get_dims(arch, 'mesh_3d', 3), wrap=False)


class Torus2DInterconnect(CoordinateInterconnect):
    def __init__(self, parent, name, clusters, arch):
        super().__init__(parent, name, clusters, arch, 'torus_2d', get_dims(arch, 'torus_2d', 2), wrap=True)


class Torus3DInterconnect(CoordinateInterconnect):
    def __init__(self, parent, name, clusters, arch):
        super().__init__(parent, name, clusters, arch, 'torus_3d', get_dims(arch, 'torus_3d', 3), wrap=True)


class Ruche2DInterconnect(CoordinateInterconnect):
    def __init__(self, parent, name, clusters, arch):
        dims = get_dims(arch, 'ruche_2d', 2)
        hop = _ruche_hop(arch, 'ruche_2d')
        _check_ruche_dims(dims, hop, 'ruche_2d')
        offsets = [(hop, 0), (0, hop)]
        super().__init__(parent, name, clusters, arch, 'ruche_2d', dims, wrap=False,
                         extra_offsets=offsets, ruche_hop=hop)


class Ruche3DInterconnect(CoordinateInterconnect):
    def __init__(self, parent, name, clusters, arch):
        dims = get_dims(arch, 'ruche_3d', 3)
        hop = _ruche_hop(arch, 'ruche_3d')
        _check_ruche_dims(dims, hop, 'ruche_3d')
        offsets = [(hop, 0, 0), (0, hop, 0), (0, 0, hop)]
        super().__init__(parent, name, clusters, arch, 'ruche_3d', dims, wrap=False,
                         extra_offsets=offsets, ruche_hop=hop)


class HexaMeshInterconnect(CoordinateInterconnect):
    def __init__(self, parent, name, clusters, arch):
        dims = get_dims(arch, 'hexa_mesh', 2)
        super().__init__(parent, name, clusters, arch, 'hexa_mesh', dims, wrap=False,
                         extra_offsets=[(1, -1)], diagonal_mode='hexa')


class HexaTorusInterconnect(CoordinateInterconnect):
    def __init__(self, parent, name, clusters, arch):
        dims = get_dims(arch, 'hexa_torus', 2)
        super().__init__(parent, name, clusters, arch, 'hexa_torus', dims, wrap=True,
                         extra_offsets=[(1, -1)], diagonal_mode='hexa')


class OctaMeshInterconnect(CoordinateInterconnect):
    def __init__(self, parent, name, clusters, arch):
        dims = get_dims(arch, 'octa_mesh', 2)
        super().__init__(parent, name, clusters, arch, 'octa_mesh', dims, wrap=False,
                         extra_offsets=[(1, -1), (1, 1)], diagonal_mode='octa')


class OctaTorusInterconnect(CoordinateInterconnect):
    def __init__(self, parent, name, clusters, arch):
        dims = get_dims(arch, 'octa_torus', 2)
        super().__init__(parent, name, clusters, arch, 'octa_torus', dims, wrap=True,
                         extra_offsets=[(1, -1), (1, 1)], diagonal_mode='octa')
