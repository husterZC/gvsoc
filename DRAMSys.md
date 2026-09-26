## [Look Here!] How to get start GVSoC with DRAMSys5 Library

### Getting Started

For a streamlined experience, you can build, run, and test the dramsys-integrated GVSoC simply by following instructions below:

**On the ETH Network:**

- Run the command:
	~~~~~shell
	source dramsys_pushbutton_ETHenv.sh
	~~~~~
    Note: If you encounter errors related to Python packages not being found, please install the necessary packages yourself.

**Outside the ETH Network:**

Ensure your system meets the following environment requirements:

- GCC version 11.2.0 or higher
- G++ version 11.2.0 or higher
- CMake version 3.18.1 or higher

Once these requirements are met, proceed by using:

~~~~~shell
source dramsys_pushbutton.sh
~~~~~

### Change DRAM Models

To help you get start with GVSoC+DRAMSys, we prepared GVSoC target `pulp-open-ddr` is a template model of Pulp-SoC connected with external DRAM models. The default configuration utilizes one HBM2 channel for the DRAM model. If you want to use different DRAM models, you can modify the code in `core/models/memory/dramsys.py` 

```python
import gvsoc.systree as st

class Dramsys(st.Component):

    def __init__(self, parent, name):

        super(Dramsys, self).__init__(parent, name)

        self.set_component('memory.dramsys')

        self.add_properties({
            'require_systemc': True,
            'dram-type': 'hbm2',
        })
```

You can change the `dram-type` to `ddr3`, `ddr4`, `hbm2`, `lpddr4`, which they models DRAM of:

- `ddr3` : DDR3 DIMM (8 x MICRON_DDR3 Chips, 1GB capacity, 1600MHz-DDR)
- `ddr4` : DDR4 DIMM (8 x JEDEC_SPEC_DDR4 Chips, 4GB capacity, 1866MHz-DDR )
- `hbm2` : Single Channel of HBM2 Stack (1GB capacity, 2000MHz-DDR)
- `lpddr4` : Single Channle of LPDDR4 Chip (1GB capacity, 3200MHz-DDR)


### Develop your GVSoC+DRAM simulation target

Once you have successfully go through `source dramsys_pushbutton.sh`, the environment for GVSoC+DRAMSys co-simulation is setup

Then You can freely develop your own model, instance multiple DRAM models and connect them. You can follow the example in `pulp/pulp/chips/pulp_open/pulp_open.py`

```python
80		ddr = memory.dramsys.Dramsys(self, 'ddr') 
130		self.bind(soc_clock, 'out', ddr, 'clock')
164		self.bind(soc, 'ddr', ddr, 'input')
```

**Note:** If you opened a new terminal/shell to your workplace, please do `source sourceme.sh` before building your GVSoC target, this will make sure neccesary environment parameters set properly for GVSoC+DRAMSys co-simulation.

### Configuration and library paths

The current Python API selects a simulation JSON with `dram_type`, for example
`memory.dramsys.Dramsys(self, 'ddr', dram_type='hbm2-example.json')`.
The corresponding component property is `dram-type`.

- `make build-configs` copies `add_dramsyslib_patches/dramsys_configs` into
  `core/models/memory/dramsys_configs`.
- By default, the core model uses that source configuration directory. Set
  `DRAMSYS_PATH` to use another directory containing a `dramsys_configs/`
  subdirectory. For example, `DRAMSYS_PATH=/path/to/run` selects
  `/path/to/run/dramsys_configs/<dram_type>`.
- `libDRAMSys_Simulator.so` is found through the dynamic loader.
  `source sourceme.sh` adds `third_party/DRAMSys` to `LD_LIBRARY_PATH`.
- `SYSTEMC_HOME` selects the SystemC installation used by the GVSoC build.
  Its version and C++ ABI must match the chosen DRAMSys library.

### IO_v2 HBM4 network benchmark

The [3D network tests](pulp/pulp/3d_network/README.md#dramsys-hbm4-endpoints)
use these same path conventions. Their `DramsysEndpoint` handles the 512-bit
AXI beat interface, native DRAM burst assembly and response backpressure.
An endpoint is configured with
`DramsysEndpoint(..., dram_type='hbm4-emu-example.json', data_width=512)`.

The recorded HBM4 measurements use the **bundled**
`add_dramsyslib_patches/libDRAMSys_Simulator.so` with **SystemC 2.3.3, C++17**.
This binary has the older two-argument `add_dram` API. The newer source-build
recipe uses SystemC 3.0.1 and the three-argument memspec API used by the core
wrapper; it is not a drop-in replacement for this endpoint. Use the compatible
pair when reproducing these measurements.

For that bundled-library setup, install the bundled binary into the usual
`third_party/DRAMSys` directory if no DRAMSys library is installed there yet,
then run from the GVSoC root:

```bash
mkdir -p third_party/DRAMSys
cp -n add_dramsyslib_patches/libDRAMSys_Simulator.so third_party/DRAMSys/
source sourceme.sh
export SYSTEMC_HOME=/path/to/systemc-2.3.3-built-with-cxx17
export PYTHON=/path/to/python-with-gvsoc-dependencies
bash pulp/pulp/3d_network/tests/hbm4.sh build
bash pulp/pulp/3d_network/tests/hbm4.sh run
```

The source configuration is
`add_dramsyslib_patches/dramsys_configs/hbm4-emu-example.json`, including its
referenced memspec. The bundled library needs basename references and `clkMhz`;
the HBM4 address map also needs byte addressing consistent with the memspec's
32-bit word. `prepare_hbm4.py` makes these conversions and disables simulation
instrumentation in a generated copy, preserving all source timing values and
recording source/resolved hashes. The launcher sets `DRAMSYS_PATH` to
`build/network3d_hbm4` for that copy. An explicit `DRAMSYS_PATH` selects an
existing prepared configuration and is never overwritten by the launcher.

For a different prepared simulation JSON, use `--parameter=dram_type=<file.json>`.
`pulp/pulp/3d_network/doc/hbm4_config` is an archived measurement snapshot;
normal builds and runs do not use it.


### Migrate DRAMSys-Integration to other GVSoC branches

There are a lot of on-developing GVSoC branchs for different extensions. To enbale GVSoC+DRAMsys co-simulations on those branches, there are following steps to follow:

- copy folder `add_dramsyslib_patches`
- copy `dramsys_pushbutton_ETHenv.sh` and `dramsys_pushbutton.sh`
- Merge the `## Make Targets for DRAMSys Integration ##` part in Makefile
- Merge the `## Envirment Parameters for DRAMSys Integration ##` part in `sourceme.sh`
- Try to run `make dramsys_apply_patch`
	- if you encounter any errors, please try modify `core` & `pulp` submodules accroding to `add_dramsyslib_patches/gvsoc_core.patch` and `add_dramsyslib_patches/gvsoc_pulp.patch`, and generate new patches replacing the old one in `add_dramsyslib_patches`
- After all setps above, you can run `source dramsys_pushbutton_ETHenv.sh` or `source dramsys_pushbutton.sh` to build, run, and test the dramsys-integrated GVSoC on your branch.
