cmake_minimum_required(VERSION 3.16)
project(velocity_software_collective_tree C)

set(SOFTWARE_COLLECTIVE_TREE_TEST_MAX_ALL_GROUP "16" CACHE STRING
    "Maximum clusters used by the all-rank collective regression group; 0 means no cap.")
set(SOFTWARE_COLLECTIVE_TREE_TEST_MAX_SUBSET_GROUP "8" CACHE STRING
    "Maximum clusters used by the subset collective regression group; 0 means no cap.")

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/include/software_collective_tree_test_config.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/software_collective_tree_test_config.h
    @ONLY
)

set(SRC_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/main.c
)

set(SOURCES ${SRC_SOURCES} PARENT_SCOPE)
set(INCLUDE_DIRS ${CMAKE_CURRENT_BINARY_DIR}/generated PARENT_SCOPE)
