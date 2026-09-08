#!/usr/bin/env bash

set -euo pipefail

readonly ROOT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
readonly SRC_DIR="${ROOT_DIR}/zion-enclave"
readonly BUILD_DIR="${ZION_ENCLAVE_BUILD_DIR:-${ROOT_DIR}/output/zion-enclave-build}"
readonly SDK_BUILD_DIR="${BUILD_DIR}/sdk"
readonly SDK_INSTALL_DIR="${BUILD_DIR}/sdk-install"
readonly EYRIE_BUILD_DIR="${BUILD_DIR}/eyrie"
readonly EYRIE_OUTPUT_DIR="${EYRIE_BUILD_DIR}/output"
readonly EXAMPLES_BUILD_DIR="${BUILD_DIR}/examples"
readonly ARTIFACT_DIR="${BUILD_DIR}/artifacts"
readonly INITRAMFS_ROOT="${BUILD_DIR}/initramfs-root"
readonly GUEST_KERNEL_DIR="${ZION_GUEST_KERNEL_DIR:-${ROOT_DIR}/zion-guest}"
readonly BASE_INITRAMFS="${ZION_BASE_INITRAMFS:-${ROOT_DIR}/utils/env/initrd-ssh.img}"
readonly CROSS_COMPILE="${CROSS_COMPILE:-riscv64-unknown-linux-gnu-}"
readonly JOBS="${JOBS:-$(nproc)}"
readonly RISCV_FLAGS="-march=rv64gc_zifencei -mabi=lp64d"

require_tool()
{
	command -v "$1" >/dev/null 2>&1 || {
		echo "error: required tool not found: $1" >&2
		exit 1
	}
}

build_sdk()
{
	rm -rf "${SDK_BUILD_DIR}" "${SDK_INSTALL_DIR}"
	cmake -S "${SRC_DIR}/sdk" -B "${SDK_BUILD_DIR}" \
		-DCROSS_COMPILE="${CROSS_COMPILE}" \
		-DCMAKE_C_COMPILER="${CROSS_COMPILE}gcc" \
		-DCMAKE_CXX_COMPILER="${CROSS_COMPILE}g++" \
		-DCMAKE_ASM_COMPILER="${CROSS_COMPILE}gcc" \
		-DZION_BITS=64 \
		-DZION_SDK_DIR="${SDK_INSTALL_DIR}" \
		-DCMAKE_INSTALL_PREFIX="${SDK_INSTALL_DIR}"
	cmake --build "${SDK_BUILD_DIR}" -j"${JOBS}"
	cmake --install "${SDK_BUILD_DIR}"
}

build_runtime()
{
	mkdir -p "${EYRIE_OUTPUT_DIR}"
	cmake -S "${SRC_DIR}/runtime" -B "${EYRIE_BUILD_DIR}" \
		-DCMAKE_C_COMPILER="${CROSS_COMPILE}gcc" \
		-DCMAKE_ASM_COMPILER="${CROSS_COMPILE}gcc" \
		-DCMAKE_OBJCOPY="$(command -v "${CROSS_COMPILE}objcopy")" \
		-DCMAKE_SYSTEM_NAME=Linux \
		-DCMAKE_SYSTEM_PROCESSOR=riscv64 \
		-DCMAKE_C_FLAGS="${RISCV_FLAGS}" \
		-DCMAKE_ASM_FLAGS="${RISCV_FLAGS}" \
		-DZION_SDK_DIR="${SDK_INSTALL_DIR}" \
		-DEYRIE_OUTPUT_DIR="${EYRIE_OUTPUT_DIR}"
	cmake --build "${EYRIE_BUILD_DIR}" -j"${JOBS}"
}

build_driver()
{
	make -C "${GUEST_KERNEL_DIR}" \
		ARCH=riscv CROSS_COMPILE="${CROSS_COMPILE}" \
		M="${SRC_DIR}/driver/kernel" \
		ZION_SDK_DIR="${SRC_DIR}/sdk" clean
	make -C "${GUEST_KERNEL_DIR}" -j"${JOBS}" \
		ARCH=riscv CROSS_COMPILE="${CROSS_COMPILE}" \
		M="${SRC_DIR}/driver/kernel" \
		ZION_SDK_DIR="${SRC_DIR}/sdk" modules
	mkdir -p "${ARTIFACT_DIR}"
	install -m 0644 "${SRC_DIR}/driver/kernel/zion-driver.ko" \
		"${ARTIFACT_DIR}/zion-driver.ko"
}

build_examples()
{
	make -C "${SRC_DIR}/examples" hello \
		CROSS_COMPILE="${CROSS_COMPILE}" \
		SDK_DIR="${SDK_INSTALL_DIR}" \
		BUILD_DIR="${EXAMPLES_BUILD_DIR}" \
		RUNTIME_DIR="${EYRIE_OUTPUT_DIR}"
}

package_initramfs()
{
	local enclave_dir

	[ -f "${BASE_INITRAMFS}" ] || {
		echo "error: base initramfs not found: ${BASE_INITRAMFS}" >&2
		exit 1
	}
	for file in zion-driver.ko; do
		[ -f "${ARTIFACT_DIR}/${file}" ] || {
			echo "error: missing driver artifact: ${file}" >&2
			exit 1
		}
	done
	for file in hello hello-runner; do
		[ -f "${EXAMPLES_BUILD_DIR}/${file}" ] || {
			echo "error: missing example artifact: ${file}" >&2
			exit 1
		}
	done
	for file in eyrie-rt loader.bin; do
		[ -f "${EYRIE_OUTPUT_DIR}/${file}" ] || {
			echo "error: missing runtime artifact: ${file}" >&2
			exit 1
		}
	done

	rm -rf "${INITRAMFS_ROOT}"
	mkdir -p "${INITRAMFS_ROOT}"
	(
		cd "${INITRAMFS_ROOT}"
		gzip -dc "${BASE_INITRAMFS}" | cpio -idm --quiet --no-absolute-filenames
	)
	enclave_dir="${INITRAMFS_ROOT}/root/zion-enclave"
	mkdir -p "${enclave_dir}"
	install -m 0644 "${ARTIFACT_DIR}/zion-driver.ko" "${enclave_dir}/"
	install -m 0755 "${EXAMPLES_BUILD_DIR}/hello" \
		"${EXAMPLES_BUILD_DIR}/hello-runner" \
		"${EYRIE_OUTPUT_DIR}/eyrie-rt" \
		"${EYRIE_OUTPUT_DIR}/loader.bin" "${enclave_dir}/"
	install -m 0755 "${SRC_DIR}/test/run-nested-hello.sh" \
		"${INITRAMFS_ROOT}/usr/bin/run-zion-enclave-test"
	chmod 0755 "${INITRAMFS_ROOT}"
	(
		cd "${INITRAMFS_ROOT}"
		find . -print0 | cpio --null -o --format=newc --owner=0:0 --quiet | \
			gzip -9 > "${BUILD_DIR}/initrd-enclave.img"
	)
	echo "enclave initramfs: ${BUILD_DIR}/initrd-enclave.img"
}

usage()
{
	printf 'Usage: %s {all|sdk|runtime|driver|examples|package|clean}\n' "$0"
}

for tool in cmake make gzip cpio find "${CROSS_COMPILE}gcc" \
	"${CROSS_COMPILE}g++" "${CROSS_COMPILE}objcopy"; do
	require_tool "${tool}"
done

case "${1:-all}" in
	all)
		build_sdk
		build_runtime
		build_driver
		build_examples
		package_initramfs
		;;
	sdk) build_sdk ;;
	runtime) build_runtime ;;
	driver) build_driver ;;
	examples) build_examples ;;
	package) package_initramfs ;;
	clean) rm -rf "${BUILD_DIR}" ;;
	*) usage >&2; exit 2 ;;
esac
