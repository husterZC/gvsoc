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
ifdef app
	app_path = $(abspath $(app))
	sw_cmake_arg = "-DSRC_DIR=$(app_path)"
endif

arch_cmake_arg := "-DRISCV_ARCH=rv32imafdv_zfh"

sw:
	rm -rf sw_build && mkdir sw_build
	cd sw_build && $(CMAKE) $(sw_cmake_arg) $(arch_cmake_arg) ../velocity/sw/ && make
	@! grep -q "ebreak" sw_build/velocity.dump || (echo "Error: 'ebreak' found in sw_build/velocity.dump" && exit 1)

clean_sw:
	rm -rf sw_build


######################################################################
## 				Make Targets for Run Simulator		 				##
######################################################################

run:
	./install/bin/gvsoc --target=pulp.chips.velocity.velocity_target --binary sw_build/velocity.elf run

rund:
	./install/bin/gvsoc --target=pulp.chips.velocity.velocity_target --binary sw_build/velocity.elf run --trace-level=6 --trace=system/cluster_0/core/insn

