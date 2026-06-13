# Velocity Unified Interconnect Topologies

Topology classes in this directory reuse the existing topology-agnostic
`UnifiedRouter` and `UnifiedLink` models. Each class builds routers, links,
cluster endpoints, and a destination-indexed route table.

## Selecting A Topology

Set one of the following in a `VelocityArch` file:

```python
self.unified_interco = "torus_2d"
```

or:

```python
self.unified_interco = True
self.unified_interco_topology = "torus_2d"
```

Leaving `self.unified_interco = None` keeps the legacy flat DMA router.

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
self.unified_interco_dims = (x, y)       # 2D families
self.unified_interco_dims = (x, y, z)    # 3D families
self.unified_interco_hop = 2             # Ruche H-hop distance
self.unified_interco_ring_size = 16
self.unified_interco_tree_radix = 4
self.unified_interco_tree_level = 3
self.unified_interco_hypercube_dims = 4
```

Topology-specific names such as `unified_interco_torus_2d_dims` and
`unified_interco_ruche_3d_hop` override the common parameters.

Dragonfly uses:

```python
self.unified_interco_dragonfly_groups = 4
self.unified_interco_dragonfly_routers_per_group = 4
self.unified_interco_dragonfly_terminals_per_router = 1
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
self.unified_interco_torus_2d_routing = "wrap_tree"
self.unified_interco_ring_routing = "wrap_tree"
```

`wrap_tree` routes over a deadlock-safe spanning tree chosen from the physical
wrap graph, so wrap links can participate without requiring virtual channels in
the router model.

For shortest wrap-around paths, use:

```python
self.unified_interco_torus_2d_routing = "wrap_minimal"
self.unified_interco_torus_3d_routing = "wrap_minimal"
self.unified_interco_ring_routing = "wrap_minimal"
```

`wrap_minimal` uses wrap-around links aggressively. In a strict wormhole NoC
model this normally requires virtual channels/dateline handling for formal
deadlock freedom; the current `UnifiedRouter` has one route table and no virtual
channel state.

Dragonfly defaults to `tree` routing for the same reason. Set:

```python
self.unified_interco_dragonfly_routing = "minimal"
```

to use the deterministic local/global/local dragonfly route.
