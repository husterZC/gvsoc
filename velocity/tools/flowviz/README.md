# Velocity FlowViz

Velocity FlowViz visualizes packet movement from `make runv` flow traces.

It takes:

- a Velocity `arch.py`
- a `packet_flow_trace.txt`

and starts a local browser UI with chip boxes, clusters, the configured
on-chip/off-chip routers and links, packet animation, timeline scrubbing, speed
control, zoom/pan, filters, and packet hover details.

## Run

From the repository root:

```bash
eval "$(/usr/local/anaconda3/bin/conda shell.bash hook)"
source init.sh
make flowviz
```

Then open the printed local URL. The page starts with arch and trace fields plus
a file picker rooted at the repository `velocity/` directory.

You can also start the module directly:

```bash
python3 -m velocity.tools.flowviz --no-browser
```

Preload inputs when using a known architecture and trace:

```bash
make flowviz flowviz_arch=/path/to/velocity_arch_multi_chip_ring_N2_C4.py flowviz_trace=/path/to/packet_flow_trace.txt
```

Use `--port 0` to bind any free port:

```bash
python3 -m velocity.tools.flowviz \
  --port 0 \
  --no-browser
```

To start the file picker from a different directory:

```bash
python3 -m velocity.tools.flowviz \
  --browse-root velocity/tests/regression/results \
  --no-browser
```

## Export Parsed JSON

```bash
python3 -m velocity.tools.flowviz \
  --arch velocity/hw/velocity_arch.py \
  --trace tmp/packet_flow_trace.txt \
  --export tmp/flowviz.json \
  --no-server
```

## Topology And Trace Notes

The visualizer builds the configured on-chip and off-chip interconnect graph
from `arch.py`, so inactive context links are still visible when a trace only
exercises part of the system. It also accepts observed component paths such as:

```text
chip_0/onchip_interco/cluster_0_to_level_0_0/flow
chip_0/onchip_interco/level_0_0_to_level_1_0/flow
offchip_interco/cluster_0_to_router_0/flow
offchip_interco/router_0_to_router_1/flow
offchip_interco/router_1_to_cluster_1/flow
```

All chips and clusters come from `arch.py`, with `SystemInfo` lines in the trace
used as an override when the run used a generated regression architecture.

Packet IDs follow the `runv` trace tags:

- DMA/RDMA: `dma:<src>:<packet_id>`
- In-network collective: `coll:<root>:<seq>:<op>:<src>`
