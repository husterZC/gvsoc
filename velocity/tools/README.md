# Velocity Tools

This directory contains runnable helper tools used by the Velocity build,
debug, and visualization flows.

- `config.py`: generates runtime architecture headers from a Velocity
  `arch.py` file. It is invoked by `make config`.
- `flowviz/`: browser-based packet flow visualizer for `make runv` traces.

Keep tool-specific implementation files inside the tool directory that owns
them. Add shared import-only Python code in a separate `velocity/common/`
package only when multiple tools actually need it.
