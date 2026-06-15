# Understanding Velocity In-Router Collectives

This note explains the current topology-aware in-router collective
implementation under `velocity/hw/unified_interco`.

The short answer is:

**The router and link building blocks are still topology-agnostic.** Topology
builders provide two generic pieces of metadata:

1. The normal unicast route table:

   ```text
   routes[destination_cluster] = output_port
   ```

2. A route-derived subtree bitset for each root cluster:

   ```text
   collective_subtree[root][router] = source clusters whose route to root
                                     passes through this router
   ```

At runtime, routers combine this metadata with the packet's group descriptor.
Fanout collectives form branch packets by grouping destinations that share the
same next output. Reduce and gather aggregate at every router whose subtree
contains more than one still-needed contribution.

There is no topology-specific code in `router.cpp`: meshes, tori, fat trees,
rings, trees, dragonfly, and hypercube all use the same route table and subtree
metadata interface.

## Files Involved

- `velocity/sw/runtime/include/collective_innetwork.h`
  - User-facing collective helpers. This layer is unchanged.
- `velocity/hw/velocity_dma.cpp`
  - Builds the public in-network collective packets from DMA register state.
    This layer is unchanged.
- `velocity/hw/collective_packet.hpp`
  - Defines the compact public `InNetworkHeader`, operation IDs, group
    descriptors, group helper functions, and flags.
- `velocity/hw/unified_interco/router.py`
  - Passes route tables, local-cluster output metadata, and collective subtree
    bitsets into each C++ router instance.
- `velocity/hw/unified_interco/router.cpp`
  - Implements branch fanout, internal subset packets, and subtree aggregation.
- `velocity/hw/unified_interco/topologies/base.py`
  - Computes subtree masks for the graph-based topology families by tracing the
    already-built route table.
- `velocity/hw/unified_interco/topologies/fat_tree.py`
  - Computes the same subtree masks for the custom fat-tree builder.

## Public Packet Header

Every public in-network collective packet begins with the 32-byte
`InNetworkHeader`.

Important fields:

- `op`: broadcast, reduce-int8-sum, scatter, gather, or all-to-all.
- `root_cluster`: root cluster selected by software. All-to-all uses group rank
  0 as a metadata root.
- `src_cluster`: original source cluster for source-sensitive operations.
- `group_type`, `group_base`, `group_count`, `group_stride`,
  `group_outer_count`, `group_outer_stride`: compact group descriptor.
- `offset`: receive offset.
- `bytes`: bytes per rank contribution or slice.
- `seq`: sequence number for matching one collective instance.
- `flags`:
  - `INNETWORK_FLAG_DIRECT`: public point-to-point collective packet.
  - `INNETWORK_FLAG_FINAL`: final reduce/gather result.
  - `INNETWORK_FLAG_SUBSET`: router-internal packet with a rank bitmap after
    the public header.

The header stays 32 bytes. `INNETWORK_FLAG_SUBSET` does not add fields to the
header; it only says that this router-to-router packet has an internal bitmap
immediately after the header. Direct and final packets delivered to DMA do not
carry this internal bitmap.

## Group Rank Order

The group helpers in `collective_packet.hpp` define rank order by ascending
cluster ID, not topology coordinate:

```text
ALL, num_cluster=8
  ranks: 0 1 2 3 4 5 6 7

CONTIGUOUS_RANGE base=2 count=3
  ranks: 2 3 4

STRIDE base=0 count=4 stride=2
  ranks: 0 2 4 6

NESTED_STRIDE base=0 inner_count=2 inner_stride=1 outer_count=2 outer_stride=4
  ranks: 0 1 4 5
```

This rank order is used for scatter payload slices, gather output packing, and
all-to-all receive slots.

## Build-Time Subtree Planning

The topology builders first compute ordinary routes exactly as before. After
routes are complete, they derive collective subtree masks by tracing the route
from every source cluster to every possible root cluster.

Conceptually:

```text
for root_cluster in clusters:
  for src_cluster in clusters:
    router = router_attached_to(src_cluster)
    while router is not the root cluster sink:
      collective_subtree[root_cluster][router].add(src_cluster)
      router = next router on routes[router][root_cluster]
```

The stored bitset is indexed by source cluster ID. The C++ router intersects it
with the packet's communication group at runtime, so the topology builder does
not need to know which collective groups software will use.

This preserves the decoupling:

- topology builders know graph shape and route tables;
- routers know only route tables, local output metadata, and opaque bitsets;
- software and DMA APIs remain topology-agnostic.

## Internal Subset Packets

Router-to-router packets may carry a compact rank subset:

```text
InNetworkHeader, with INNETWORK_FLAG_SUBSET set
rank_bitmap[ceil(group_count / 8)]
payload packed for the ranks in rank_bitmap
```

The bitmap is in group-rank space, not cluster-ID space. This keeps it compact
for all supported group descriptors and avoids adding a large cluster mask to
the public header.

Payload layout:

- Broadcast: one `bytes` payload, independent of subset size.
- Reduce: one reduced `bytes` payload for the subset.
- Scatter/all-to-all: `subset_count * bytes`, packed in ascending group-rank
  order.
- Gather: `subset_count * bytes`, packed in ascending group-rank order.

When a branch contains only one rank, the router strips the subset metadata and
sends an ordinary `DIRECT` packet to that destination cluster.

## Fanout Collectives

Broadcast, scatter, and all-to-all use dynamic branch fanout.

At each router:

1. Decode the packet's current rank subset.
2. For every rank in that subset, expand `rank -> member_cluster`.
3. Look up `route_cluster(member_cluster)`.
4. Bucket ranks by next output port.
5. Emit one packet per bucket:
   - one-rank bucket: direct public packet to the member cluster;
   - multi-rank bucket: internal subset packet to a representative member in
     that bucket.

The representative is only used to choose the current router's output. The next
router re-evaluates every rank in the subset using its own route table.

### Broadcast

The root DMA injects one payload. Routers branch the rank subset by next output.
Every final branch is a direct packet carrying the same `bytes` payload.

### Scatter

The root DMA injects `group_count * bytes`. Routers keep only the slices needed
by each branch. A multi-rank branch carries those slices packed by group rank.

### All-To-All

Every member injects one packet containing `group_count * bytes`. Each source
packet fans out like scatter. Destination DMA uses `src_cluster` to place each
incoming slice into the receive slot for the source rank.

## Reduce And Gather

Reduce and gather now aggregate at every router where the route-derived subtree
contains multiple relevant group ranks.

For an incoming packet at router `R`:

1. Decode the incoming rank subset.
2. Compute the expected subset:

   ```text
   expected = group_ranks whose member_cluster is in
              collective_subtree[root_cluster][R]
   ```

3. Validate that the incoming subset is contained in `expected`.
4. Merge the contribution into router state keyed by operation, group, root,
   sequence, offsets, and byte count.
5. When all ranks in `expected` have arrived:
   - if this is the root-side router, emit one final direct packet to root DMA;
   - otherwise, emit one internal subset aggregate toward `root_cluster`.

### Reduce Int8 Sum

Each incoming subset carries one `bytes` partial sum. The router adds it into
the local aggregate once, then marks all ranks in that subset as seen.

When complete, a non-root router forwards one partial sum for its whole subtree.
The root-side router forwards one final direct result to root DMA.

### Gather

Each incoming subset carries `subset_count * bytes`, packed by group rank. The
router copies each rank's slice into the packed position for the router's
expected subset.

When complete, a non-root router forwards a packed gather segment for its
subtree. The root-side router's expected subset is the whole group, so its final
payload matches the software-visible gather layout.

## Root-Side Router Detection

The router still detects whether it is adjacent to the root cluster with:

```cpp
output = route_cluster(root_cluster)
output_clusters[output] == root_cluster
```

This remains useful only for deciding when to emit the final public
reduce/gather packet. Intermediate completion is driven by the subtree bitset,
not by topology-specific router IDs.

## Fallback Behavior

If a router is instantiated without subtree metadata, reduce and gather fall
back to the old conservative behavior: non-root routers forward direct packets
toward the root, and the root-side router aggregates. Current supported
topology builders provide subtree metadata, so the full regression topology set
uses the topology-aware path.

## Example: Reduce On A Strided Group

Assume:

```text
num_cluster = 8
group = STRIDE(base=0, count=4, stride=2)
members = [0, 2, 4, 6]
root = 4
bytes = 64
```

Build time:

- The topology computes `routes[R][4]` for every router `R`.
- It also computes `collective_subtree[4][R]`, the source clusters whose path
  to root cluster 4 passes through `R`.

Runtime:

1. Clusters 0, 2, 4, and 6 inject one reduce contribution each.
2. A leaf or intermediate router computes:

   ```text
   expected = {group ranks whose member clusters pass through this router}
   ```

3. If expected is `{0, 1}`, that router waits for clusters 0 and 2, sums them,
   and forwards one internal subset packet for ranks `{0, 1}`.
4. Another branch may independently forward ranks `{2, 3}`.
5. The root-side router receives the completed branch aggregates, sums them,
   and sends one final direct packet to root cluster 4.

The exact routers involved depend only on the route table generated by the
topology. No collective-specific topology code is needed.

## Important Consequences

1. Shared-path traffic is reduced for fanout operations.

   Broadcast/scatter/all-to-all no longer split immediately into one direct
   packet per final member when several members share the same next output.

2. Reduce/gather aggregation is distributed.

   Intermediate routers combine contributions once their route-derived subtree
   subset is complete.

3. The public software and DMA contract is unchanged.

   DMA still sees only the original 32-byte header and public direct/final
   packets. The subset bitmap is private to router-to-router packets.

4. Topology support remains generic.

   Adding a topology only needs correct unicast routes and local cluster output
   metadata. The shared subtree tracing code can derive collective masks from
   those routes.

5. Sequence numbers still matter.

   Router aggregate state is keyed by `seq` and the operation/group metadata.
   Overlapping logical collectives with the same key would alias, just as in the
   previous implementation.

## Debugging The Path

Useful trace points:

- Router:
  - `collective_queue`
  - `collective_aggregate`
  - `collective_response`
- DMA send path:
  - `collective_tcdm_to_dma`
  - `send_prepare`
  - `send_issue`
  - `send_complete`
- Destination DMA:
  - `collective_hold`
  - `collective_dma_to_tcdm`
  - `collective_complete`

For a packet, track:

```text
op, seq, src_cluster, root_cluster, flags, req.addr / cluster_stride
```

For internal subset packets, the actual branch membership is in the bitmap
after the header. Direct and final packets do not carry that bitmap.

## Verification

The implementation was verified with:

```bash
python3 velocity/tests/regression/run_matrix.py \
  --mode full \
  --test hardware_collective_innetwork \
  --jobs 2 \
  --timeout 7200
```

The full topology set passed: 16 passed, 0 failed.

## Detailed Implementation Walkthrough

This section describes the new method more mechanically, as if you were
following one packet through the router.

### The Two Plans Used By A Router

The router does not receive a pre-built collective tree object. It derives the
runtime behavior from two smaller plans.

The first plan is the normal unicast route table:

```text
routes[dst_cluster] = output_port
```

This answers: "If I want to eventually reach cluster D, which output port
should I use now?"

The second plan is the collective subtree bitset:

```text
collective_subtree_words[root_cluster][source_cluster]
```

For each router instance, this bitset answers: "If the collective root is R,
which source clusters route through this router on their way to R?"

These two plans are enough:

- fanout uses the unicast route table to decide how to split a destination rank
  set by next output;
- reduce/gather uses the subtree bitset to decide which source ranks this
  router must collect before forwarding one aggregate upward.

The router then intersects both with the packet's group descriptor at runtime.
That runtime intersection is important: topology metadata is computed for all
clusters, while the packet may involve only a contiguous, strided, nested, or
power-of-two group.

### Rank Subsets

Internally, the router represents "which group members are still inside this
packet" as a rank bitmap:

```text
rank 0 -> bit 0
rank 1 -> bit 1
...
rank N -> bit N
```

Ranks are group ranks, not cluster IDs. For example, if the group is
`STRIDE(base=0, count=4, stride=2)`, the rank mapping is:

```text
rank 0 -> cluster 0
rank 1 -> cluster 2
rank 2 -> cluster 4
rank 3 -> cluster 6
```

An internal packet for ranks `{1, 3}` therefore carries a bitmap in rank space,
not a cluster mask for clusters `{2, 6}`. This keeps the internal metadata small:

```text
bitmap bytes = ceil(group_count / 8)
```

This bitmap is private to routers. It is inserted after the 32-byte public
header only when `INNETWORK_FLAG_SUBSET` is set. DMA never needs to parse it.

### Packet Decode Rules

When a router receives an in-network collective packet, it first reconstructs a
`CollectiveView`:

```text
header
subset bitmap
payload pointer
payload size
group count
subset count
```

The subset comes from one of three places:

1. Internal subset packet:

   ```text
   header.flags has INNETWORK_FLAG_SUBSET
   subset = bitmap after header
   payload = bytes after bitmap
   ```

2. Public direct reduce/gather packet:

   ```text
   header.flags has INNETWORK_FLAG_DIRECT
   subset = one rank: rank(header.src_cluster)
   payload = bytes after header
   ```

3. Original non-direct source packet:

   ```text
   broadcast/scatter/all-to-all: subset = full group
   reduce/gather: subset = one rank: rank(header.src_cluster)
   ```

This lets one router routine handle both first-hop DMA packets and intermediate
router packets.

### Fanout Algorithm

Broadcast, scatter, and all-to-all use the same branch algorithm.

For each rank in the current subset:

```text
member_cluster = innetwork_group_member(header, rank)
output = route_cluster(member_cluster)
bucket[output].add(rank)
```

Then the router emits one packet per bucket.

If the bucket has one rank:

```text
emit public DIRECT packet to that member cluster
no subset bitmap
payload is exactly the single destination payload
```

If the bucket has multiple ranks:

```text
emit internal SUBSET packet
destination address uses one representative member in the bucket
payload is packed for only the ranks in that bucket
```

The representative member is not a semantic destination for the collective. It
is only used to make this hop choose the bucket's output port. At the next
router, the bitmap is decoded and the same bucketing process runs again using
that router's route table.

This is how shared-prefix traffic is reduced without teaching the router what
kind of topology it is in.

### Fanout Payload Handling

Broadcast is simple:

```text
payload is one bytes-sized value
every branch forwards the same payload
```

Scatter and all-to-all are slice-based:

```text
source packet payload:
  rank 0 slice
  rank 1 slice
  rank 2 slice
  ...
```

When a router emits a branch subset, it repacks only the selected ranks in
ascending group-rank order. Example:

```text
incoming subset = {0, 1, 2, 3}
outgoing bucket = {1, 3}
outgoing payload = slice(rank 1), slice(rank 3)
```

The next router interprets that outgoing payload relative to the outgoing
subset bitmap. It does not assume the payload still contains full-group layout.

### Reduce/Gather Expected Subset

For reduce and gather, each router computes:

```text
expected = {}
for each group rank:
  member = group_member(rank)
  if collective_subtree[root_cluster][member] contains this router:
    expected.add(rank)
```

This `expected` subset is the exact set of group ranks whose route to the root
passes through the current router.

An incoming contribution is legal only if:

```text
incoming_subset is a subset of expected
```

The router then accumulates state until:

```text
seen == expected
```

Only then does it forward upward. This is the distributed aggregation rule.

### Reduce State

Reduce state stores one `bytes`-sized accumulator per active collective key.

When an incoming packet arrives:

```text
for i in 0..bytes-1:
  accumulator[i] += incoming_payload[i]
seen |= incoming_subset
```

The incoming payload is already a complete partial sum for its subset. It may
represent one cluster or a whole downstream branch.

When complete:

```text
if this is root-side router:
  emit FINAL | DIRECT public packet to root_cluster
else:
  emit SUBSET internal packet toward root_cluster
  subset = expected
  payload = accumulator
```

The root-side router is the router whose route to `root_cluster` exits through
a local cluster output. Only that router emits the public final packet.

### Gather State

Gather state stores `expected_count * bytes`.

Incoming gather payload is packed for `incoming_subset`, so the router copies
rank slices one by one:

```text
for rank in incoming_subset:
  source_index = packed_index(incoming_subset, rank)
  target_index = packed_index(expected, rank)
  state[target_index] = incoming_payload[source_index]
seen |= incoming_subset
```

When complete:

```text
if this is root-side router:
  emit FINAL | DIRECT public packet to root_cluster
  payload is full group gather layout
else:
  emit SUBSET internal packet toward root_cluster
  subset = expected
  payload is packed for expected
```

The root-side router's expected subset should be the whole group, because every
group member's path to the root cluster passes through the router attached to
that root cluster.

### Why The Method Is Topology-Aware

The method is topology-aware because every split and every aggregate completion
is derived from the route table created by the topology builder.

For fanout, if several destination clusters leave the current router through
the same output port, they remain one packet for this hop.

For reduce/gather, if several source clusters' paths to the root merge at a
router, that router sees them in the same `expected` subset and can aggregate
before forwarding.

For a different topology, the route table and traced subtrees are different, so
the collective branch and merge points are different.

## Adding A New Topology

A new topology does not need to implement collective operations itself. It only
needs to provide the generic routing metadata correctly.

### Preferred Path: Inherit From `GraphTopologyInterconnect`

If the topology can be expressed as a graph of routers and links, inherit from
`GraphTopologyInterconnect` in `topologies/base.py`.

Use the base helpers to build the graph:

```python
node = self._new_node(...)
self._connect_router_pair(left, right)
self._connect_cluster(cluster_id, router)
```

Then compute routes using one of the existing finalizers:

```python
self._finalize_next_hop_routes(next_hop)
self._finalize_shortest_routes()
self._finalize_spanning_tree_routes()
```

Those finalizers call `_instantiate_routers()`, and `_instantiate_routers()`
calls `_compute_collective_subtrees()` automatically. If your topology uses
these hooks, topology-aware collectives are enabled without extra collective
code.

### Custom Builder Path

If the topology cannot inherit from `GraphTopologyInterconnect`, follow the
fat-tree pattern.

The custom builder must maintain these structures:

```python
self.node_order
self._graph
self._cluster_router
node.routes
node.output_clusters
```

Their meaning must match the shared builders:

- `self.node_order`: every router node in the topology.
- `self._graph[src_router_name]`: outgoing edges as `(dst_name, output_port)`.
- `self._cluster_router[cluster_id]`: router node directly attached to that
  cluster.
- `node.routes[cluster_id]`: output port from this router toward that cluster.
- `node.output_clusters[output_port]`: cluster ID if that output goes directly
  to a local cluster, otherwise `-1`.

After route computation and before router instantiation:

```python
self._compute_collective_subtrees()
```

Then pass the metadata into each `UnifiedRouter`:

```python
UnifiedRouter(
    ...,
    routes=node.routes,
    output_clusters=output_clusters,
    collective_subtree_words=self._collective_subtrees[node.name],
    collective_subtree_words_per_root=self._collective_subtree_words_per_root,
)
```

### Required Routing Properties

The subtree derivation assumes deterministic per-destination routing:

```text
for a given router and destination cluster, routes[destination] is one output
```

For every source cluster and root cluster, following `routes[root_cluster]`
from the source-attached router must eventually reach the root cluster sink.

The route must not cycle. The subtree computation explicitly checks for cycles
while tracing a source-to-root path.

Every route output must correspond to a valid outgoing edge in `_graph`, and
the final hop to a cluster must be represented as the cluster sink name:

```text
cluster_0
cluster_1
...
```

This is how the subtree tracer knows when it has reached the root cluster.

### Local Cluster Output Metadata

`output_clusters` is required for final delivery.

When connecting a cluster to a router, the topology must mark the router output
that goes directly to the cluster:

```python
router.output_clusters[out_port] = cluster_id
```

The C++ router uses this to detect the root-side router:

```cpp
output = route_cluster(root_cluster);
is_root_side = output_clusters[output] == root_cluster;
```

If this metadata is missing, reduce/gather may build partial aggregates but not
know which router should emit the final public packet.

### Adaptive Or Multipath Routing

The current collective subtree metadata is derived from one deterministic
route-table path per destination. If a new topology wants adaptive or multipath
routing, it must still provide a deterministic collective route view, or it
must extend the metadata scheme.

Acceptable options:

1. Use adaptive routing for normal traffic, but expose deterministic
   `routes[cluster]` entries for collectives.
2. Pick one canonical next hop per destination for the route table used by the
   current router implementation.
3. Extend both subtree construction and router forwarding so the runtime path
   and the expected aggregation subsets stay consistent.

The important invariant is:

```text
the path used to forward reduce/gather packets must match the path used to
compute collective_subtree_words
```

If those diverge, a router may wait for ranks that will never pass through it,
or forward an aggregate before all downstream ranks have arrived.

### New Topology Checklist

Before expecting in-router collectives to work on a new topology, verify:

1. Every cluster is connected and appears in `_cluster_router`.
2. Every router has a valid `routes[cluster]` entry for every cluster.
3. Every `routes[cluster]` output appears in `_graph` for that router.
4. Following `routes[root]` from any source-attached router reaches
   `cluster_<root>` without cycles.
5. Direct cluster output ports are recorded in `node.output_clusters`.
6. `UnifiedRouter` receives `collective_subtree_words` and
   `collective_subtree_words_per_root`.
7. The topology passes `hardware_collective_innetwork` in the regression
   runner.

The minimum validation command is:

```bash
python3 velocity/tests/regression/run_matrix.py \
  --mode full \
  --test hardware_collective_innetwork \
  --topology <new_topology_name> \
  --jobs 1 \
  --timeout 7200
```

### Common New-Topology Failure Modes

- Missing `_cluster_router` entry:
  - subtree construction cannot start source-to-root tracing.
- Route output not present in `_graph`:
  - subtree tracing cannot find the next router or cluster sink.
- Local output not recorded in `output_clusters`:
  - root-side router detection fails.
- Runtime routing differs from traced routing:
  - reduce/gather expected subsets are wrong.
- Group rank layout is confused with topology coordinates:
  - scatter, gather, or all-to-all payload ordering fails.

If fanout collectives pass but reduce/gather hang or fail, first inspect the
subtree metadata path from sources to the root. If reduce/gather pass but
scatter/all-to-all fail, first inspect rank-to-payload slice packing.
