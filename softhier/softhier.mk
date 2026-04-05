######################################################################
## 				Make Targets for Velocity Simulator 				##
######################################################################

config_file ?= "softhier/hw/softhier_arch.py"
ifdef cfg
	config_file = "$(cfg)"
endif

config:
	rm -rf pulp/pulp/chips/softhier
	cp -rf softhier/hw pulp/pulp/chips/softhier
	cp $(config_file) pulp/pulp/chips/softhier/softhier_arch.py
	python3 softhier/utils/config.py $(config_file)

hw:
	make config
	make TARGETS=pulp.chips.softhier.softhier_target all

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
	cd sw_build && $(CMAKE) $(sw_cmake_arg) $(arch_cmake_arg) ../softhier/sw/ && make
	@! grep -q "ebreak" sw_build/softhier.dump || (echo "Error: 'ebreak' found in sw_build/softhier.dump" && exit 1)

clean_sw:
	rm -rf sw_build


######################################################################
## 				Make Targets for Run Simulator		 				##
######################################################################

run:
	./install/bin/gvsoc --target=pulp.chips.softhier.softhier_target --binary sw_build/softhier.elf run

rund:
	./install/bin/gvsoc --target=pulp.chips.softhier.softhier_target --binary sw_build/softhier.elf run --trace-level=6 --trace=insn

runi:
	./install/bin/gvsoc --target=pulp.chips.softhier.softhier_target --binary sw_build/softhier.elf run --trace-level=6 --trace=/system/cluster_0/core_2/insn

runn:
	./install/bin/gvsoc --target=pulp.chips.softhier.softhier_target --binary sw_build/softhier.elf run --trace-level=6 --trace=/system/noc

runm:
	./install/bin/gvsoc --target=pulp.chips.softhier.softhier_target --binary sw_build/softhier.elf run --trace-level=6 --trace=idma
