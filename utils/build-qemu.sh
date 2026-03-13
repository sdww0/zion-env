#! /bin/bash

# In zion-qemu container:

source utils/common.sh

mkdir -p output

if [[ " ${BUILD_OPTIONS[@]} " =~ " QEMU " ]]; then
    echo "Building QEMU..."
else
    echo "Skipping QEMU build..."
    exit 0
fi

set -e

# QEMU

mkdir -p /usr/local/qemu
pushd qemu
if [ ! -d "build" ]; then
    ./configure --target-list=riscv64-softmmu --prefix=/usr/local/qemu
fi
make -j$(nproc)
make install
popd

cp -r /usr/local/qemu ${OUTPUT_DIR}
