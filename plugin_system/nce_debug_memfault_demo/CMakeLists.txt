#
# Copyright (c) 2024 1NCE
#

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(nce_debug_memfault_demo)

# NORDIC SDK APP START
add_subdirectory(src)
# NORDIC SDK APP END

zephyr_include_directories(src)
zephyr_include_directories(config)
