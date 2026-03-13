#! /bin/bash

source utils/common.sh

mkdir -p output 

if [[ " ${BUILD_OPTIONS[@]} " =~ " BOOTLOADER " ]]; then
    echo "Building BOOTLOADER..."
else
    echo "Skipping BOOTLOADER build..."
    exit 0
fi

BOARD=("qemu" "megrez")

set -e

# For milkv megrez

if [[ " ${BOARD[@]} " =~ " megrez " ]]; then
    # U-Boot
    pushd u-boot
    make clean
    make ${CONFIG_FILE}
    sed -i "s#eswin/eic7700-evb-a2.dtb#eswin/${DT_NAME}.dtb#" .config
    make -j$(nproc)
    cp u-boot.bin $OUTPUT_DIR/u-boot-megrez.bin
    cp u-boot.dtb $OUTPUT_DIR/u-boot-megrez.dtb
    popd

    # OpenSBI
    pushd opensbi
    make clean
    make PLATFORM=generic FW_PAYLOAD=y \
        FW_FDT_PATH=$OUTPUT_DIR/u-boot-megrez.dtb \
        FW_PAYLOAD_PATH=$OUTPUT_DIR/u-boot-megrez.bin \
        -j$(nproc)
    cp build/platform/generic/firmware/fw_payload.bin $OUTPUT_DIR/fw_payload-megrez.bin
    cp build/platform/generic/firmware/fw_payload.elf $OUTPUT_DIR/fw_payload-megrez.elf
    popd

    # nsign
    pushd opensbi
    cp $OUTPUT_DIR/fw_payload-megrez.bin sign/preload/fw_payload.bin
    pushd sign/preload
    sed -i "s#HOLDER#$(pwd)#g" bootchain.cfg
    ../nsign bootchain.cfg
    cp -v bootloader_secboot_ddr5.bin $OUTPUT_DIR/bootloader_secboot_ddr5_$NAME.bin
    # Write back the original placeholder
    sed -i "s#$(pwd)#HOLDER#g" bootchain.cfg
    popd
    popd
fi

# For QEMU virt

if [[ " ${BOARD[@]} " =~ " qemu " ]]; then
    # U-Boot
    pushd u-boot
    make clean
    make qemu-riscv64_defconfig
    make -j$(nproc)
    cp u-boot-nodtb.bin $OUTPUT_DIR/u-boot-nodtb-qemu.bin
    popd

    # OpenSBI
    pushd opensbi
    make clean
    make PLATFORM=generic \
        FW_PAYLOAD_PATH=$OUTPUT_DIR/u-boot-nodtb-qemu.bin \
        -j$(nproc)
    cp build/platform/generic/firmware/fw_dynamic.bin $OUTPUT_DIR/fw_dynamic-qemu.bin
    popd
fi

