cmake_minimum_required(VERSION 3.16)
project(velocity_zero_load_latency C)

set(SRC_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/main.c
)

set(SOURCES ${SRC_SOURCES} PARENT_SCOPE)
