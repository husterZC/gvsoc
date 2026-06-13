from pulp.chips.velocity.unified_interco.topologies.dragonfly import DragonflyInterconnect
from pulp.chips.velocity.unified_interco.topologies.fat_tree import FatTreeInterconnect
from pulp.chips.velocity.unified_interco.topologies.hypercube import HypercubeInterconnect
from pulp.chips.velocity.unified_interco.topologies.mesh import (
    HexaMeshInterconnect,
    HexaTorusInterconnect,
    Mesh2DInterconnect,
    Mesh3DInterconnect,
    OctaMeshInterconnect,
    OctaTorusInterconnect,
    Ruche2DInterconnect,
    Ruche3DInterconnect,
    Torus2DInterconnect,
    Torus3DInterconnect,
)
from pulp.chips.velocity.unified_interco.topologies.ring import RingInterconnect
from pulp.chips.velocity.unified_interco.topologies.tree import TreeInterconnect


TOPOLOGIES = {
    'fat_tree': FatTreeInterconnect,
    'fattree': FatTreeInterconnect,
    'mesh_2d': Mesh2DInterconnect,
    '2d_mesh': Mesh2DInterconnect,
    'mesh_3d': Mesh3DInterconnect,
    '3d_mesh': Mesh3DInterconnect,
    'torus_2d': Torus2DInterconnect,
    '2d_torus': Torus2DInterconnect,
    'torus_3d': Torus3DInterconnect,
    '3d_torus': Torus3DInterconnect,
    'ruche_2d': Ruche2DInterconnect,
    'h_hop_2d_ruche_mesh': Ruche2DInterconnect,
    '2d_ruche': Ruche2DInterconnect,
    'ruche_3d': Ruche3DInterconnect,
    'h_hop_3d_ruche_mesh': Ruche3DInterconnect,
    '3d_ruche': Ruche3DInterconnect,
    'hexa_mesh': HexaMeshInterconnect,
    'hexamesh': HexaMeshInterconnect,
    'hexa_torus': HexaTorusInterconnect,
    'hexatorus': HexaTorusInterconnect,
    'octa_mesh': OctaMeshInterconnect,
    'octamesh': OctaMeshInterconnect,
    'octa_torus': OctaTorusInterconnect,
    'octatorus': OctaTorusInterconnect,
    'ring': RingInterconnect,
    'tree': TreeInterconnect,
    'dragonfly': DragonflyInterconnect,
    'hypercube': HypercubeInterconnect,
}


def normalize_topology_name(name: str) -> str:
    return name.lower().replace('-', '_').replace(' ', '_')


def create_interconnect(parent, name, clusters, arch):
    requested = getattr(arch, 'unified_interco', None)
    topology = requested if isinstance(requested, str) else getattr(arch, 'unified_interco_topology', requested)

    if requested is True and topology is None:
        topology = 'fat_tree'
    if topology is True:
        topology = 'fat_tree'
    if not isinstance(topology, str):
        raise ValueError(f'Unsupported Velocity unified_interco topology: {topology}')

    normalized = normalize_topology_name(topology)
    interconnect = TOPOLOGIES.get(normalized)
    if interconnect is None:
        supported = ', '.join(sorted(TOPOLOGIES))
        raise ValueError(f'Unsupported Velocity unified_interco topology: {topology}. Supported: {supported}')

    return interconnect(parent, name, clusters, arch)


__all__ = [
    'DragonflyInterconnect',
    'FatTreeInterconnect',
    'HexaMeshInterconnect',
    'HexaTorusInterconnect',
    'HypercubeInterconnect',
    'Mesh2DInterconnect',
    'Mesh3DInterconnect',
    'OctaMeshInterconnect',
    'OctaTorusInterconnect',
    'RingInterconnect',
    'Ruche2DInterconnect',
    'Ruche3DInterconnect',
    'TOPOLOGIES',
    'Torus2DInterconnect',
    'Torus3DInterconnect',
    'TreeInterconnect',
    'create_interconnect',
    'normalize_topology_name',
]
