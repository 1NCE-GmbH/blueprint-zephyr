#!/bin/bash

west build --build-dir /workdir/project/build plugin_system/nce_fota_mender_demo -b thingy91_nrf9160_ns -- -DNCS_TOOLCHAIN_VERSION=NONE -DCONF_FILE="prj.conf"
cat ./plugin_system/nce_fota_mender_demo/prj.conf
# Step 1: Update prj.conf
sed -i 's/CONFIG_APPLICATION_VERSION=2/CONFIG_APPLICATION_VERSION=1/' ./plugin_system/nce_fota_mender_demo/prj.conf
sed -i 's/CONFIG_ARTIFACT_NAME="release-v2"/CONFIG_ARTIFACT_NAME="release-v1"/' ./plugin_system/nce_fota_mender_demo/prj.conf

# Step 2: Build the project with the new configurations
west build --build-dir /workdir/project/build1 plugin_system/nce_fota_mender_demo -b thingy91_nrf9160_ns -- -DNCS_TOOLCHAIN_VERSION=NONE -DCONF_FILE="prj.conf"
cat ./plugin_system/nce_fota_mender_demo/prj.conf

west build --build-dir /workdir/project/build_udp nce_udp_demo -b thingy91_nrf9160_ns -- -DNCS_TOOLCHAIN_VERSION=NONE -DCONF_FILE="prj.conf"
west build --build-dir /workdir/project/build_coap nce_coap_demo -b thingy91_nrf9160_ns -- -DNCS_TOOLCHAIN_VERSION=NONE -DCONF_FILE="prj.conf"

# Step 3: Copy generated files to the target directory
ls
cp ./build/zephyr/app_signed.hex ./plugin_system/nce_fota_mender_demo/thingy_binaries/
cp ./build/zephyr/app_update.bin ./plugin_system/nce_fota_mender_demo/thingy_binaries/app_update_v2.bin
cp ./build1/zephyr/app_update.bin ./plugin_system/nce_fota_mender_demo/thingy_binaries/app_update_v1.bin
cp ./build_coap/zephyr/app_signed.hex ./nce_coap_demo/thingy_binaries/
cp ./build_udp/zephyr/app_signed.hex ./nce_udp_demo/thingy_binaries/
ls ./nce_udp_demo/thingy_binaries/
ls ./nce_coap_demo/thingy_binaries/
ls ./plugin_system/nce_fota_mender_demo/thingy_binaries
# Step 4: Create a Mender artifact
mender-artifact write module-image -t thingy -o ./plugin_system/nce_fota_mender_demo/thingy_binaries/v1.mender -T release-v1 -n release-v1 -f ./plugin_system/nce_fota_mender_demo/thingy_binaries/app_update_v1.bin --compression none
mender-artifact write module-image -t thingy -o ./plugin_system/nce_fota_mender_demo/thingy_binaries/v2.mender -T release-v2 -n release-v2 -f ./plugin_system/nce_fota_mender_demo/thingy_binaries/app_update_v2.bin --compression none
