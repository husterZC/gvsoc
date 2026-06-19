######################################################################
## 				Make Targets for Velocity Simulator 				##
######################################################################

config_file ?= "velocity/hw/velocity_arch.py"
ifdef cfg
	config_file = "$(cfg)"
endif

.PHONY: config hw sw clean_sw run rund runv flowviz

config:
	rm -rf pulp/pulp/chips/velocity
	cp -rf velocity/hw pulp/pulp/chips/velocity
	cp $(config_file) pulp/pulp/chips/velocity/velocity_arch.py
	python3 velocity/tools/config.py $(config_file)

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

run_trace_args ?= --trace-level=trace --trace=chip_.*/cluster_.*/dma/trace --trace=chip_.*/cluster_0/rdma/trace --trace=chip_.*/cluster_.*/tcdm/dma_converter/trace --trace=chip_.*/onchip_interco/.*/trace --trace=offchip_interco/.*/trace
runv_trace_args ?= --trace-level=trace --trace=chip_.*/cluster_.*/dma/flow --trace=chip_.*/cluster_0/rdma/flow --trace=chip_.*/onchip_interco/.*/flow --trace=offchip_interco/.*/flow
flowviz_arch ?=
flowviz_trace ?=
flowviz_host ?= 127.0.0.1
flowviz_port ?= 8765
flowviz_open ?= 0
flowviz_browser_arg := $(if $(filter 1 true yes,$(flowviz_open)),,--no-browser)
flowviz_arch_arg := $(if $(flowviz_arch),--arch $(flowviz_arch),)
flowviz_trace_arg := $(if $(flowviz_trace),--trace $(flowviz_trace),)
flowviz_args ?=

run:
	./install/bin/gvsoc --target=pulp.chips.velocity.velocity_target --binary $(sw_build_dir)/velocity.elf run

rund:
	mkdir -p $(sw_build_dir)
	bash -o pipefail -c './install/bin/gvsoc --target=pulp.chips.velocity.velocity_target --binary $(sw_build_dir)/velocity.elf run $(run_trace_args) 2>&1 | tee $(sw_build_dir)/analyze_trace.txt'

runv:
	mkdir -p $(sw_build_dir)
	bash -o pipefail -c './install/bin/gvsoc --target=pulp.chips.velocity.velocity_target --binary $(sw_build_dir)/velocity.elf run $(runv_trace_args) 2>&1 | tee $(sw_build_dir)/packet_flow_trace.txt'

flowviz:
	python3 -B -m velocity.tools.flowviz $(flowviz_arch_arg) $(flowviz_trace_arg) --host $(flowviz_host) --port $(flowviz_port) $(flowviz_browser_arg) $(flowviz_args)


######################################################################
## 				Make Targets for Velocity Regression				##
######################################################################

regression_dir := velocity/tests/regression
regression_results_dir := $(regression_dir)/results
REGRESSION_TIMEOUT ?= $(if $(filter command line environment environment override,$(origin TIMEOUT)),$(TIMEOUT),)
REGRESSION_PROGRESS ?= $(if $(filter command line environment environment override,$(origin PROGRESS)),$(PROGRESS),)
REGRESSION_COLOR ?= $(if $(filter command line environment environment override,$(origin COLOR)),$(COLOR),)
REGRESSION_RUN_TARGET ?= $(if $(filter command line environment environment override,$(origin RUN_TARGET)),$(RUN_TARGET),)

regression_smoke_jobs := $(if $(JOB),$(JOB),$(if $(JOBS),$(JOBS),0))
regression_full_jobs := $(if $(JOB),$(JOB),$(if $(JOBS),$(JOBS),1))

.PHONY: smoke full regression regression_smoke regression_full clean_results clean_regression_results

smoke: regression_smoke

full: regression_full

regression: regression_smoke regression_full

regression_smoke:
	JOBS="$(regression_smoke_jobs)" TIMEOUT="$(REGRESSION_TIMEOUT)" PROGRESS="$(REGRESSION_PROGRESS)" COLOR="$(REGRESSION_COLOR)" $(regression_dir)/run_smoke.sh

regression_full:
	JOBS="$(regression_full_jobs)" TIMEOUT="$(REGRESSION_TIMEOUT)" RUN_TARGET="$(REGRESSION_RUN_TARGET)" PROGRESS="$(REGRESSION_PROGRESS)" COLOR="$(REGRESSION_COLOR)" $(regression_dir)/run_full.sh

clean_results: clean_regression_results

clean_regression_results:
	rm -rf $(regression_results_dir)
