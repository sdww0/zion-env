#!/usr/bin/env bash

set -euo pipefail

readonly ROOT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
readonly KERNEL_IMAGE="${ZION_KERNEL_IMAGE:-localhost/zion-kernel:0.1.0}"
readonly QEMU_IMAGE="${ZION_QEMU_IMAGE:-localhost/zion-qemu:0.1.0}"

build_kernel=true
build_qemu=true

usage()
{
	cat <<'EOF'
Usage: ./build-all.sh [--all | --kernel | --qemu]

  --all      Build bootloader, host/guest kernels, TVM driver, and QEMU
  --kernel   Build bootloader, host/guest kernels, and TVM driver only
  --qemu     Build QEMU only
  -h, --help Show this help

Container images can be overridden with ZION_KERNEL_IMAGE and
ZION_QEMU_IMAGE.
EOF
}

case "${1:---all}" in
	--all)
		;;
	--kernel)
		build_qemu=false
		;;
	--qemu)
		build_kernel=false
		;;
	-h|--help)
		usage
		exit 0
		;;
	*)
		usage >&2
		exit 2
		;;
esac

if [ "$#" -gt 1 ]; then
	usage >&2
	exit 2
fi

if ! command -v podman >/dev/null 2>&1; then
	echo "error: podman is required" >&2
	exit 1
fi

require_repo()
{
	if [ ! -d "${ROOT_DIR}/$1/.git" ]; then
		echo "error: missing source repository: $1" >&2
		echo "run ./download.sh first" >&2
		exit 1
	fi
}

require_image()
{
	if ! podman image exists "$1"; then
		echo "error: missing container image: $1" >&2
		echo "build the images with: (cd utils/docker && ./docker-build.sh)" >&2
		exit 1
	fi
}

if $build_kernel; then
	require_repo opensbi
	require_repo u-boot
	require_repo zion-host
	require_repo zion-guest
	require_image "$KERNEL_IMAGE"

	echo "==> Building bootloader, kernels, and TVM driver"
	podman run --rm \
		--platform linux/amd64 \
		--volume "${ROOT_DIR}:/root" \
		--workdir /root \
		"$KERNEL_IMAGE" \
		bash -euc $'bash ./utils/build-bootloader.sh\nbash ./utils/build-kernel.sh'
fi

if $build_qemu; then
	require_repo qemu
	require_image "$QEMU_IMAGE"

	echo "==> Building QEMU"
	podman run --rm \
		--platform linux/riscv64 \
		--volume "${ROOT_DIR}:/root" \
		--workdir /root \
		"$QEMU_IMAGE" \
		bash ./utils/build-qemu.sh
fi

echo "==> Build complete; artifacts are under ${ROOT_DIR}/output"
