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

podman run -it --name zion-qemu -v .:/root zion-qemu &

sleep 3

set -e

# QEMU
podman exec -it zion-qemu bash -c '

set -e

mkdir -p /usr/local/qemu
pushd qemu
if [ ! -d "build" ]; then
    ./configure --target-list=riscv64-softmmu --prefix=/usr/local/qemu
fi
make -j$(nproc)
make install
popd

source utils/common.sh
cp -r /usr/local/qemu ${OUTPUT_DIR}
'

podman stop zion-qemu
podman rm zion-qemu
