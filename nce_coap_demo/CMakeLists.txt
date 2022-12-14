
cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(nce-coap-client)

target_sources(app PRIVATE src/main.c)
target_sources(app PRIVATE src/interface/udp_interface_zephyr.c)
zephyr_include_directories(src/interface)
