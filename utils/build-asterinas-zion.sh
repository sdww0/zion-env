#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
ROOT_DIR="$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)"
if [[ ${1:-} == --help ]]; then
	echo "Usage: bash $0"
	echo 'Optional env: ASTER_LOG_LEVEL, ASTER_RELEASE, ASTER_INITRAMFS, ASTER_BUILD_CACHE, PODMAN_IMAGE'
	exit 0
fi
[[ $# == 0 ]] || { echo 'No positional arguments are supported.' >&2; exit 2; }
ASTERINAS_DIR="$ROOT_DIR/asterinas"
ASTERINAS_TEST_DIR="$ROOT_DIR/patch/zion-test-scripts/zion-tests/asterinas-test"

PODMAN_IMAGE="${PODMAN_IMAGE:-docker.io/asterinas/asterinas:0.18.0-20260701}"
ASTER_INITRAMFS="${ASTER_INITRAMFS:-$ASTERINAS_TEST_DIR/asterinas_initramfs.cpio.gz}"
ASTER_CONSOLE="${ASTER_CONSOLE:-ttyS0}"
ASTER_LOG_LEVEL="${ASTER_LOG_LEVEL:-error}"
ASTER_RELEASE="${ASTER_RELEASE:-1}"
ASTER_BUILD_CACHE="${ASTER_BUILD_CACHE:-$ROOT_DIR/output/asterinas-build-cache}"

if [ ! -d "$ASTERINAS_DIR" ]; then
	echo "missing Asterinas source tree: $ASTERINAS_DIR" >&2
	exit 1
fi

if [ ! -f "$ASTER_INITRAMFS" ]; then
	echo "missing Zion Asterinas initramfs: $ASTER_INITRAMFS" >&2
	exit 1
fi

if ! command -v podman >/dev/null 2>&1; then
	echo "podman is required to build Asterinas" >&2
	exit 1
fi

mkdir -p "$ASTER_BUILD_CACHE/cargo"
mkdir -p "$ASTER_BUILD_CACHE/rustup"

ASTER_INITRAMFS=$(realpath "$ASTER_INITRAMFS")
ASTER_BUILD_CACHE=$(realpath "$ASTER_BUILD_CACHE")
container_initramfs=/zion-initramfs.cpio.gz
container_cache=/zion-build-cache
release_arg=""
profile_dir="debug"
if [ "$ASTER_RELEASE" = "1" ]; then
	release_arg="--release"
	profile_dir="release"
fi

podman run --rm --privileged --network=host \
	-v /dev:/dev \
	-v "$ROOT_DIR:/work" \
	-v "$ASTER_INITRAMFS:$container_initramfs:ro" \
	-v "$ASTER_BUILD_CACHE:$container_cache" \
	"$PODMAN_IMAGE" \
	bash -lc "
		set -euo pipefail
		export CARGO_HOME='$container_cache/cargo'
		export RUSTUP_HOME='$container_cache/rustup'
		export PATH=\"\$CARGO_HOME/bin:\$PATH\"
		mkdir -p \"\$CARGO_HOME\" \"\$RUSTUP_HOME\"
		cd /work/asterinas
		if ! command -v cargo-osdk >/dev/null 2>&1; then
			make install_osdk
		fi
		cd kernel
		OSDK_TARGET_ARCH=riscv64 cargo osdk build \
			--scheme riscv \
			--grub-boot-protocol=multiboot2 \
			$release_arg \
			--kcmd-args='ostd.log_level=$ASTER_LOG_LEVEL' \
			--kcmd-args='console=$ASTER_CONSOLE' \
			--initramfs='$container_initramfs'
	"

kernel_bin="$ASTERINAS_DIR/target/riscv64imac-unknown-none-elf/$profile_dir/aster-kernel-osdk-bin"
if [ ! -f "$kernel_bin" ]; then
	echo "missing built kernel binary: $kernel_bin" >&2
	exit 1
fi

mkdir -p "$ASTERINAS_TEST_DIR"
cp "$kernel_bin" "$ASTERINAS_TEST_DIR/aster-kernel-osdk-bin"
cp "$kernel_bin" "$ASTERINAS_TEST_DIR/asterinas_kernel"

echo "Synced Zion Asterinas kernel artifacts:"
echo "  $ASTERINAS_TEST_DIR/aster-kernel-osdk-bin"
echo "  $ASTERINAS_TEST_DIR/asterinas_kernel"
