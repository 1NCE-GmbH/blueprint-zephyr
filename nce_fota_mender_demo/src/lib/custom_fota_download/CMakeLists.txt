#
# Copyright (c) 2022 Nordic Semiconductor
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#

zephyr_include_directories(include)

target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/fota_download.c)
target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/fota_download_util.c)
target_sources_ifdef(CONFIG_SECURE_BOOT
	app PRIVATE  ${ZEPHYR_BASE}/../nrf/subsys/dfu/include)

# zephyr_include_directories_ifdef(
#   ${ZEPHYR_BASE}/../nrf/subsys/dfu/include)
