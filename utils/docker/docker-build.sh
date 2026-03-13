#! /bin/bash

podman buildx build -f ./kernel.Dockerfile --platform linux/amd64 -t zion-kernel:0.1.0 . 
podman buildx build -f ./qemu.Dockerfile --platform linux/riscv64 -t zion-qemu:0.1.0 .

