#! /bin/bash

# In zion-kernel container:

source utils/common.sh

mkdir -p output

set -e

export ARCH=riscv
export CROSS_COMPILE=riscv64-unknown-linux-gnu-

# Host kernel
if [[ " ${BUILD_OPTIONS[@]} " =~ " HOST_KERNEL " ]]; then
    echo "Building host kernel..."

    pushd zion-host
    make win2030_defconfig
    make -j$(nproc)
    mkdir -p ${OUTPUT_DIR}/artifacts
    cp arch/riscv/boot/dts/eswin/*.dtb ${OUTPUT_DIR}/artifacts/
    cp arch/riscv/boot/Image ${OUTPUT_DIR}/host_kernel_image
    popd
fi

# Tvm driver
if [[ " ${BUILD_OPTIONS[@]} " =~ " TVM_DRIVER " ]]; then
    echo "Building TVM driver..."

    make -C tvm-driver clean
    make -C tvm-driver tvm-control
    make -C tvm-driver tvm-driver
    mkdir -p ${OUTPUT_DIR}/tvm-driver
    cp tvm-driver/tvm-driver.ko ${OUTPUT_DIR}/tvm-driver/tvm-driver.ko
    cp tvm-driver/tvm-control ${OUTPUT_DIR}/tvm-driver/tvm-control
fi

# Guest kernel
if [[ " ${BUILD_OPTIONS[@]} " =~ " GUEST_KERNEL " ]]; then
    echo "Building guest kernel..."

    pushd zion-guest
    make win2030_defconfig
    make -j$(nproc)
    cp arch/riscv/boot/Image ${OUTPUT_DIR}/guest_kernel_image
    popd
fi
