######################################################################
## 				Make Targets for Velocity Simulator 				##
######################################################################

config_file ?= "velocity/hw/velocity_arch.py"
ifdef cfg
	config_file = "$(cfg)"
endif

config:
	rm -rf pulp/pulp/chips/velocity
	cp -rf velocity/hw pulp/pulp/chips/velocity
	cp $(config_file) pulp/pulp/chips/velocity/velocity_arch.py
	python3 velocity/utils/config.py $(config_file)

hw:
	make config
	make TARGETS=pulp.chips.velocity.velocity_target all

######################################################################
## 				Make Targets for Velocity Software	 				##
######################################################################

sw_cmake_arg ?= ""
sw_build_dir ?= sw_build
sw_source_dir := $(abspath velocity/sw)
ifdef app
	app_path = $(abspath $(app))
	sw_cmake_arg = "-DSRC_DIR=$(app_path)"
endif

arch_cmake_arg := "-DRISCV_ARCH=rv32imafdv_zfh"

sw:
	rm -rf $(sw_build_dir) && mkdir -p $(sw_build_dir)
	cd $(sw_build_dir) && $(CMAKE) $(sw_cmake_arg) $(arch_cmake_arg) $(sw_source_dir)/ && make
	@! grep -q "ebreak" $(sw_build_dir)/velocity.dump || (echo "Error: 'ebreak' found in $(sw_build_dir)/velocity.dump" && exit 1)

clean_sw:
	rm -rf $(sw_build_dir)


######################################################################
## 				Make Targets for Run Simulator		 				##
######################################################################

run_trace_args ?= --trace-level=trace --trace=system/cluster_.*/dma/trace --trace=system/cluster_.*/tcdm/dma_converter/trace --trace=system/dma_interco/.*/trace

run:
	./install/bin/gvsoc --target=pulp.chips.velocity.velocity_target --binary $(sw_build_dir)/velocity.elf run

rund:
	mkdir -p $(sw_build_dir)
	bash -o pipefail -c './install/bin/gvsoc --target=pulp.chips.velocity.velocity_target --binary $(sw_build_dir)/velocity.elf run $(run_trace_args) 2>&1 | tee $(sw_build_dir)/analyze_trace.txt'
