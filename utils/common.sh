#! /bin/bash

export CROSS_COMPILE=riscv64-linux-gnu-
export CONFIG_FILE=eic7700_milkv_megrez_defconfig
export DT_NAME=eic7700-milkv-megrez
export NAME=milkv_megrez
export OUTPUT_DIR=$(pwd)/output
# BUILD_OPTIONS=("BOOTLOADER" "HOST_KERNEL" "GUEST_KERNEL" "TVM_DRIVER" "QEMU")
BUILD_OPTIONS=("BOOTLOADER" "GUEST_KERNEL" "HOST_KERNEL" "TVM_DRIVER" "QEMU")
