#! /bin/bash

# This script is used to test the Zion TEE environment in QEMU virtual machine.
# Require sudo permission to mount the rootfs
# 
# `./run.sh pass-compile` to run QEMU directly.
# 
# QEMU version: 10.1.93
# 

set -e

PASS_COMPILE=false
ZION_CVM_SSH_PORT="${ZION_CVM_SSH_PORT:-10022}"
ZION_OUTER_SSH_PORT="${ZION_OUTER_SSH_PORT:-10023}"

if [ "$1" == "pass-compile" ]; then
    PASS_COMPILE=true
fi

if [ "$PASS_COMPILE" = false ] ; then
    pushd ../
    ./build-all.sh

    source utils/common.sh

    set +e

    ZION_TESTS_DIR=./rootfs_ext4/root/zion-tests
    mkdir -p rootfs_ext4
    sudo mount -o loop ./virt/rootfs.ext4 rootfs_ext4
    sudo mkdir -p $ZION_TESTS_DIR

    # Load guest kernel and initrd to rootfs
    if [[ " ${BUILD_OPTIONS[@]} " =~ " GUEST_KERNEL " ]]; then
        sudo rm $ZION_TESTS_DIR/guest_kernel_image
        sudo cp $OUTPUT_DIR/guest_kernel_image $ZION_TESTS_DIR/guest_kernel_image
    fi

    # Load tvm-driver to rootfs
    if [[ " ${BUILD_OPTIONS[@]} " =~ " TVM_DRIVER " ]]; then
        sudo rm $ZION_TESTS_DIR/tvm-control
        sudo rm $ZION_TESTS_DIR/tvm-driver.ko
        sudo cp $OUTPUT_DIR/tvm-driver/tvm-driver.ko $ZION_TESTS_DIR/tvm-driver.ko
        sudo cp $OUTPUT_DIR/tvm-driver/tvm-control $ZION_TESTS_DIR/tvm-control
    fi

    # Load qemu to rootfs
    if [[ " ${BUILD_OPTIONS[@]} " =~ " QEMU " ]]; then
        sudo rm -rf ./rootfs_ext4/usr/local/qemu
        sudo cp -r $OUTPUT_DIR/qemu/ ./rootfs_ext4/usr/local/qemu
    fi

    # Environment scripts and initrd
    sudo cp ./utils/env/start_tvm_from_boot.sh $ZION_TESTS_DIR/start_tvm_from_boot.sh
    sudo cp ./utils/env/tee-sqlite.sh $ZION_TESTS_DIR/tee-sqlite.sh
    sudo cp ./utils/env/initrd.img $ZION_TESTS_DIR/initrd.img
    sudo cp ./utils/env/start_tvm_ssh_from_boot.sh $ZION_TESTS_DIR/start_tvm_ssh_from_boot.sh
    sudo cp ./utils/env/tee-ssh.sh $ZION_TESTS_DIR/tee-ssh.sh
    if [ -f ./utils/env/initrd-ssh.img ]; then
        sudo cp ./utils/env/initrd-ssh.img $ZION_TESTS_DIR/initrd-ssh.img
    fi

    # umount
    sync
    sudo umount rootfs_ext4
    rm -r rootfs_ext4
    popd
fi

qemu-system-riscv64 -smp 1 -m 4G \
    -nographic \
    -machine virt \
    -cpu rv64,pmp=true,pmp-granularity=4096,sv48=true,svpbmt=true,sstc=false \
    -bios ../output/fw_dynamic-qemu.bin \
    -kernel ../output/host_kernel_image \
    -initrd ./initrd.img-6.6.87-win2030 \
    -serial chardev:mux \
    -chardev stdio,id=mux,mux=on,signal=off,logfile=qemu.log \
    -monitor chardev:mux \
    -append "console=ttyS0 rw swiotlb=force root=/dev/sda rootfstype=ext4 rootwait selinux=0 cma=1G" \
    -drive file=./rootfs.ext4,format=raw,if=none,id=hd0 \
    -device virtio-scsi-device,id=scsi -device scsi-hd,drive=hd0 \
    -netdev user,id=net0,hostfwd=tcp:127.0.0.1:"$ZION_CVM_SSH_PORT"-:10022,hostfwd=tcp:127.0.0.1:"$ZION_OUTER_SSH_PORT"-:22 \
    -device virtio-net-device,netdev=net0
