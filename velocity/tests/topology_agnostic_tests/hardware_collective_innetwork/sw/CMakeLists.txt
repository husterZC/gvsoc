cmake_minimum_required(VERSION 3.16)
project(velocity_hardware_collective_innetwork C)

set(HARDWARE_COLLECTIVE_INNETWORK_TEST_MAX_EXHAUSTIVE_GROUP "16" CACHE STRING
    "Maximum clusters used by the full primitive regression group; 0 means no cap.")
set(HARDWARE_COLLECTIVE_INNETWORK_TEST_MAX_PATTERN_GROUP "16" CACHE STRING
    "Maximum clusters used by broadcast-only pattern groups; 0 means no cap.")

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/include/hardware_collective_innetwork_test_config.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/hardware_collective_innetwork_test_config.h
    @ONLY
)

set(SRC_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/main.c
)

set(SOURCES ${SRC_SOURCES} PARENT_SCOPE)
set(INCLUDE_DIRS ${CMAKE_CURRENT_BINARY_DIR}/generated PARENT_SCOPE)
