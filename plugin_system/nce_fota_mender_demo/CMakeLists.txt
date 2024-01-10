cmake_minimum_required(VERSION 3.20.0)

if(${BOARD} STREQUAL "thingy91_nrf9160_ns")
message(STATUS "Using static partition definition")
set(PM_STATIC_YML_FILE ${CMAKE_CURRENT_SOURCE_DIR}/thingy91_pm_static.yml)
endif()

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(application_update)
zephyr_compile_definitions(PROJECT_NAME=${PROJECT_NAME})

# NORDIC SDK APP START
target_sources(app PRIVATE src/main.c)
target_sources(app PRIVATE src/ota/update.c)
target_sources(app PRIVATE src/ota/nce_mender_client.c)
target_sources(app PRIVATE src/ota/led_control.c)
# Include application events and configuration headers
zephyr_library_include_directories(
	src/lib/custom_download_client
	src/lib/custom_fota_download
)

target_include_directories(app PRIVATE src/ota)
target_include_directories(app PRIVATE src/ota/include)
add_subdirectory(src/lib/custom_download_client)
add_subdirectory(src/lib/custom_fota_download)
# NORDIC SDK APP END
