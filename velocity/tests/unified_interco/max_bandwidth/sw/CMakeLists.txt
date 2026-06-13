cmake_minimum_required(VERSION 3.16)
project(velocity_max_bandwidth C)

set(MAX_BANDWIDTH_TEST_TXN_SIZE "4096" CACHE STRING "DMA transaction payload size in bytes")
set(MAX_BANDWIDTH_TEST_REPEAT "128" CACHE STRING "Number of DMA transactions to issue")
set(MAX_BANDWIDTH_TEST_MAX_INFLIGHT "8" CACHE STRING "Maximum in-flight DMA requests")
set(MAX_BANDWIDTH_TEST_SLOTS "8" CACHE STRING "Number of source/destination buffer slots")
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/include/max_bandwidth_test_config.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/max_bandwidth_test_config.h
    @ONLY
)

set(SRC_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/main.c
)

set(SOURCES ${SRC_SOURCES} PARENT_SCOPE)
set(INCLUDE_DIRS ${CMAKE_CURRENT_BINARY_DIR}/generated PARENT_SCOPE)
