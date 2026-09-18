cmake_minimum_required(VERSION 3.20)
project(zftl LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)

# Compiler optimization flags
if (MSVC)
    add_compile_options(/W4 /O2 /Oi /Ot /fp:fast /permissive-)
else()
    add_compile_options(-Wall -Wextra -O3 -march=native)
endif()

include_directories(include)

# zFTL Core Library
add_library(zftl_core STATIC
    src/lz4.c
    src/lz4_compressor.cpp
    src/flash_model.cpp
    src/ftl_controller.cpp
    src/bdi_engine.cpp
    src/line_table.cpp
    src/hyper_ram_controller.cpp
)

# CLI & Benchmark Executable
add_executable(zftl_sim
    src/main.cpp
)
target_link_libraries(zftl_sim PRIVATE zftl_core)

# Unit & Verification Test Suite
add_executable(zftl_tests
    src/tests.cpp
)
target_link_libraries(zftl_tests PRIVATE zftl_core)
