#!/usr/bin/env bash

set -euo pipefail

readonly UBUNTU_IMAGE="docker.io/library/ubuntu:24.04"

prepare_base_image()
{
	local platform="$1"
	local target="$2"
	local archive="$3"

	echo "==> Pulling ${UBUNTU_IMAGE} for ${platform}"
	if podman pull --platform "$platform" "$UBUNTU_IMAGE"; then
		podman tag "$UBUNTU_IMAGE" "$target"
		return
	fi

	echo "warning: pull failed; loading offline image ${archive}" >&2
	if [ ! -f "$archive" ]; then
		echo "error: fallback image archive not found: ${archive}" >&2
		exit 1
	fi
	podman load -i "$archive"

	if ! podman image exists "$target"; then
		echo "error: ${archive} did not provide image ${target}" >&2
		exit 1
	fi
}

prepare_base_image linux/amd64 localhost/ubuntu:24.04-amd64 \
	ubuntu-24.04-amd64.tar
prepare_base_image linux/riscv64 localhost/ubuntu:24.04-riscv \
	ubuntu-24.04-riscv.tar

podman buildx build -f ./kernel.Dockerfile --platform linux/amd64 \
	-t zion-kernel:0.1.0 .
podman buildx build -f ./qemu.Dockerfile --platform linux/riscv64 \
	-t zion-qemu:0.1.0 .
