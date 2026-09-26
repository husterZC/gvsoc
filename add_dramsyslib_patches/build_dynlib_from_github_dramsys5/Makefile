.DEFAULT_GOAL := all

CMAKE_VERSION := 3.28.1
CMAKE_DIR := cmake-$(CMAKE_VERSION)
CMAKE_TARBALL := $(CMAKE_DIR).tar.gz
CMAKE_DOWNLOAD_URL := https://cmake.org/files/v3.28/$(CMAKE_TARBALL)
CMAKE_BUILD_DIR := ./cmake_bin

$(CMAKE_TARBALL):
	wget $(CMAKE_DOWNLOAD_URL)

$(CMAKE_DIR): $(CMAKE_TARBALL)
	tar -xzvf $(CMAKE_TARBALL)

$(CURDIR)/$(CMAKE_BUILD_DIR)/bin/cmake: $(CMAKE_DIR)
	mkdir -p $(CMAKE_BUILD_DIR)
	cd $(CMAKE_DIR) && \
	./bootstrap --prefix=$(CURDIR)/$(CMAKE_BUILD_DIR) && \
	make && \
	make install

CMAKE = $(CURDIR)/$(CMAKE_BUILD_DIR)/bin/cmake
CMAKE_FLAGS ?= -j 16
# CMAKE = /scratch2/chi/dramsys_upgrade/cmake_bin/install/bin/cmake

DRAMSys/apps/simulator/simulator/dramsys_lib.cpp:
	git clone https://github.com/tukl-msd/DRAMSys.git && \
	cd DRAMSys && \
	git reset --hard 8565f18 && \
	git apply ../patch
	echo "DRAMSys setup done"

.PHONY: build all clean

build: $(CMAKE) DRAMSys/apps/simulator/simulator/dramsys_lib.cpp
	$(CMAKE) -S DRAMSys -B DRAMSys/build \
	-DCMAKE_BUILD_TYPE=Release \
	-DCMAKE_CXX_FLAGS=-fPIC \
	-DCMAKE_C_FLAGS=-fPIC \
	-DDRAMSYS_WITH_DRAMPOWER=ON \
	-DDRAMSYS_USE_FETCH_CONTENT_SYSTEMC=OFF \
	-DSystemCLanguage_DIR=$(SYSTEMC_HOME)/lib64/cmake/SystemCLanguage
	$(CMAKE) --build DRAMSys/build $(CMAKE_FLAGS)

DRAMSys/build/lib/libDRAMSys_Simulator.so: build

all: build

clean:
# 	if [ -d "DRAMSys" ]; then rm -rf DRAMSys; fi
	if [ -d "$(CMAKE_BUILD_DIR)" ]; then rm -rf $(CMAKE_BUILD_DIR); fi
	if [ -d "$(CMAKE_DIR)" ]; then rm -rf $(CMAKE_DIR); fi
	if [ -f "$(CMAKE_TARBALL)" ]; then rm $(CMAKE_TARBALL); fi
