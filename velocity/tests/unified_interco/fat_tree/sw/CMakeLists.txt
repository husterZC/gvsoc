cmake_minimum_required(VERSION 3.16)
project(velocity_fat_tree_all_to_all C)

set(FAT_TREE_TEST_WORDS "1" CACHE STRING "Payload words per all-to-all transfer")
set(FAT_TREE_TEST_MAX_INFLIGHT "8" CACHE STRING "Maximum in-flight DMA requests per cluster")
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/include/fat_tree_test_config.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/fat_tree_test_config.h
    @ONLY
)

set(SRC_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/main.c
)

set(SOURCES ${SRC_SOURCES} PARENT_SCOPE)
set(INCLUDE_DIRS ${CMAKE_CURRENT_BINARY_DIR}/generated PARENT_SCOPE)
