#! /bin/bash

# In zion-kernel container:

set -e

podman run -it --name zion-kernel -v .:/root zion-kernel &

sleep 3

podman exec -it zion-kernel bash ./utils/build-bootloader.sh
podman exec -it zion-kernel bash ./utils/build-kernel.sh

podman stop zion-kernel
podman rm zion-kernel

# In zion-qemu container:

podman run -it --name zion-qemu -v .:/root zion-qemu &

sleep 3

podman exec -it zion-qemu bash ./utils/build-qemu.sh

podman stop zion-qemu
podman rm zion-qemu
