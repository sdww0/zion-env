#! /bin/bash

# In rockos-kernel container:

source common.sh

set -e

podman run -it --name rockos-kernel -v .:/root rockos-kernel &

sleep 3

podman exec -it rockos-kernel bash ./utils/build-bootloader.sh
podman exec -it rockos-kernel bash ./utils/build-kernel.sh

podman stop rockos-kernel
podman rm rockos-kernel
