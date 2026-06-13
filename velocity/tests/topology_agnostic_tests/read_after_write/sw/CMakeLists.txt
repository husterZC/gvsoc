cmake_minimum_required(VERSION 3.16)
project(velocity_read_after_write C)

set(SRC_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/main.c
)

set(SOURCES ${SRC_SOURCES} PARENT_SCOPE)
set(INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/include PARENT_SCOPE)
