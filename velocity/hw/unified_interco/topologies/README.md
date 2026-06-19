# Velocity Unified Interconnect Topologies

Topology classes in this directory reuse the existing topology-agnostic
`UnifiedRouter` and `UnifiedLink` models. Each class builds routers, links,
cluster endpoints, and a destination-indexed route table.

## Selecting A Topology

Set one of the following in a `VelocityArch` file:

```python
self.onchip = "torus_2d"
```

or:

```python
self.onchip = True
self.onchip_topology = "torus_2d"
```

Leaving `self.onchip = None` keeps the legacy flat DMA router. Older
`unified_interco_*` names are still accepted as on-chip aliases.

The same topology builders are used for chip-to-chip RDMA. Configure that path
with the short `offchip*` names:

```python
self.num_chip = 2
self.offchip = "ring"
self.offchip_ring_size = self.num_chip
```

## Supported Names

- `fat_tree`
- `mesh_2d`, `mesh_3d`
- `torus_2d`, `torus_3d`
- `ruche_2d`, `ruche_3d`
- `hexa_mesh`, `hexa_torus`
- `octa_mesh`, `octa_torus`
- `ring`
- `tree`
- `dragonfly`
- `hypercube`

Aliases such as `2d_mesh`, `3d_torus`, `hexamesh`, and `octatorus` are also
accepted by the registry.

## Common Parameters

```python
self.onchip_dims = (x, y)       # 2D families
self.onchip_dims = (x, y, z)    # 3D families
self.onchip_hop = 2             # Ruche H-hop distance
self.onchip_ring_size = 16
self.onchip_tree_radix = 4
self.onchip_tree_level = 3
self.onchip_hypercube_dims = 4
```

Topology-specific names such as `onchip_torus_2d_dims` and
`onchip_ruche_3d_hop` override the common parameters. Use the same suffixes
with `offchip_` to configure the chip-to-chip interconnect.

Dragonfly uses:

```python
self.onchip_dragonfly_groups = 4
self.onchip_dragonfly_routers_per_group = 4
self.onchip_dragonfly_terminals_per_router = 1
```

The dragonfly graph connects routers fully inside each group and connects
same-index routers across groups.

## Partial Topologies

All topology builders allow partial population:

```text
num_cluster <= topology endpoint capacity
```

Clusters are attached to the first endpoint positions in deterministic order.
If `num_cluster` exceeds capacity, construction raises `ValueError`.

## Routing Modes

Mesh, ruche, hexa-mesh, octa-mesh, and hypercube use deterministic
dimension-order style routes by default.

Torus and ring topologies default to:

```python
self.onchip_torus_2d_routing = "wrap_tree"
self.onchip_ring_routing = "wrap_tree"
```

`wrap_tree` routes over a deadlock-safe spanning tree chosen from the physical
wrap graph, so wrap links can participate without requiring virtual channels in
the router model.

For shortest wrap-around paths, use:

```python
self.onchip_torus_2d_routing = "wrap_minimal"
self.onchip_torus_3d_routing = "wrap_minimal"
self.onchip_ring_routing = "wrap_minimal"
```

`wrap_minimal` uses wrap-around links aggressively. In a strict wormhole NoC
model this normally requires virtual channels/dateline handling for formal
deadlock freedom; the current `UnifiedRouter` has one route table and no virtual
channel state.

Dragonfly defaults to `tree` routing for the same reason. Set:

```python
self.onchip_dragonfly_routing = "minimal"
```

to use the deterministic local/global/local dragonfly route.
