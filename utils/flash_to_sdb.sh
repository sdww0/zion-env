#! /bin/bash

source utils/common.sh

pushd $OUTPUT_DIR
mkdir sdb1
mkdir sdb3
mount /dev/sdb1 sdb1
mount /dev/sdb3 sdb3

ZION_TESTS_DIR=./sdb3/root/zion-tests
mkdir -p $ZION_TESTS_DIR

# host kernel
if [[ " ${BUILD_OPTIONS[@]} " =~ " HOST_KERNEL " ]]; then
cp ./host_kernel_image ./sdb1/vmlinuz-6.6.87-win2030
fi

# tvm driver
if [[ " ${BUILD_OPTIONS[@]} " =~ " TVM_DRIVER " ]]; then
cp ./tvm-driver/tvm-driver.ko $ZION_TESTS_DIR/tvm-driver.ko
cp ./tvm-driver/tvm-control $ZION_TESTS_DIR/tvm-control
fi

# guest kernel
if [[ " ${BUILD_OPTIONS[@]} " =~ " GUEST_KERNEL " ]]; then
cp ./guest_kernel_image $ZION_TESTS_DIR/guest_kernel_image
fi

# QEMU binary
if [[ " ${BUILD_OPTIONS[@]} " =~ " QEMU " ]]; then
rm -rf ./sdb3/usr/local/qemu
cp -r $OUTPUT_DIR/qemu/ ./sdb3/usr/local/qemu
fi

# Environment scripts and initrd
cp ../utils/env/start_tvm_from_boot.sh $ZION_TESTS_DIR/start_tvm_from_boot.sh
cp ../utils/env/tee-sqlite.sh $ZION_TESTS_DIR/tee-sqlite.sh
cp ../utils/env/initrd.img $ZION_TESTS_DIR/initrd.img

sync
sync ./sdb1
sync ./sdb3
umount ./sdb1
umount ./sdb3
popd
