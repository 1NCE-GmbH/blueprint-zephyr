#
# Copyright (c) 2019 Nordic Semiconductor
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#
zephyr_include_directories(include)

target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/custom_download_client.c)
target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/parse.c)
target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/http.c)
target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/sanity.c)
target_sources_ifdef(CONFIG_COAP
	app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/coap.c)
target_sources_ifdef(CONFIG_CUSTOM_DOWNLOAD_CLIENT_SHELL
	app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/shell.c)
