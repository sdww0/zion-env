#!/usr/bin/env bash
# Run the pinned RV8 suite natively and in Zion/Zion enclaves under QEMU.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ZION_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ZION_DIR/build}"
source "$ZION_DIR/scripts/zion_paths.sh"

SM_FW="${SM_FW:-$BUILD_DIR/opensbi/platform/generic/firmware/fw_dynamic.bin}"
KERNEL="$(zion_resolve_native_kernel "$ZION_DIR" "$BUILD_DIR")"
BASE_ROOTFS="$(zion_resolve_zion_base_rootfs "$ZION_DIR" "$BUILD_DIR")"
TEST_RUNNER="$BUILD_DIR/examples/test-runner"
RV8_DIR="$BUILD_DIR/rv8-bench"
EYRIE_DIR="$BUILD_DIR/eyrie-rv8/output"
SHARED_DIR="$BUILD_DIR/rv8-qemu-shared"
ROOTFS="$BUILD_DIR/rv8-qemu-rootfs.cpio"
BASE_DTB="$BUILD_DIR/rv8-qemu-base.dtb"
DTB="$BUILD_DIR/rv8-qemu.dtb"
LOG="${RV8_QEMU_LOG:-$BUILD_DIR/rv8-qemu.log}"
ARTIFACT_MANIFEST="$LOG.artifacts.sha256"
RV8_QEMU_CMA_ARG="${RV8_QEMU_CMA_ARG:-cma=512M}"
RV8_QEMU_ENCLAVE_CPU="${RV8_QEMU_ENCLAVE_CPU:--1}"
PROGRAMS=(aes bigint dhrystone miniz norx primes qsort sha512)
CROSS_COMPILE="$(zion_resolve_cross_compile)"

fail() {
	echo "[RV8 QEMU] FAIL: $*" >&2
	exit 1
}

for command_name in cpio fdtput modinfo sha256sum strings timeout \
	"${CROSS_COMPILE}strip"; do
	command -v "$command_name" >/dev/null || fail "missing command: $command_name"
done

"$ZION_DIR/scripts/build_rv8_bench.sh"
"$ZION_DIR/scripts/build_eyrie_rv8.sh"

DRIVER="$(zion_resolve_native_driver "$ZION_DIR" "$BUILD_DIR")"
DRIVER_BUILD_MANIFEST="${ZION_DRIVER_MANIFEST:-$DRIVER.manifest}"
grep -aFq 'zion,trusted-memory' "$DRIVER" ||
	fail "driver lacks trusted-memory discovery: $DRIVER"

for path in "$SM_FW" "$KERNEL" "$BASE_ROOTFS" "$DRIVER" \
	"$DRIVER_BUILD_MANIFEST" \
	"$TEST_RUNNER" "$EYRIE_DIR/eyrie-rt" "$EYRIE_DIR/loader.bin"; do
	[ -s "$path" ] || fail "missing artifact: $path"
done

mkdir -p "$SHARED_DIR/rv8"
install -m 0644 "$DRIVER" "$SHARED_DIR/zion-driver.ko"
install -m 0644 "$DRIVER_BUILD_MANIFEST" \
	"$SHARED_DIR/zion-driver.ko.build-manifest"
install -m 0755 "$TEST_RUNNER" "$SHARED_DIR/test-runner"
install -m 0755 "$EYRIE_DIR/eyrie-rt" "$SHARED_DIR/eyrie-rv8"
install -m 0644 "$EYRIE_DIR/loader.bin" "$SHARED_DIR/loader-rv8.bin"
for program in "${PROGRAMS[@]}"; do
	install -m 0755 "$RV8_DIR/$program" "$SHARED_DIR/rv8/$program"
done
# Debug information accounts for most of the payload and is not needed by the
# benchmark.  Strip before testing so QEMU and hardware execute byte-identical
# compact files and the Megrez Android RAM-boot image remains below its limit.
"${CROSS_COMPILE}strip" "$SHARED_DIR/test-runner" \
	"$SHARED_DIR/eyrie-rv8" "${PROGRAMS[@]/#/$SHARED_DIR/rv8/}"
(
	cd "$SHARED_DIR"
	sha256sum zion-driver.ko test-runner eyrie-rv8 loader-rv8.bin \
		zion-driver.ko.build-manifest \
		rv8/aes rv8/bigint rv8/dhrystone rv8/miniz rv8/norx rv8/primes \
		rv8/qsort rv8/sha512 > MANIFEST.sha256
	sha256sum -c MANIFEST.sha256 >/dev/null
)

if [ ! -f "$ROOTFS" ] || [ "$BASE_ROOTFS" -nt "$ROOTFS" ] || \
	[ "$SCRIPT_DIR/rv8-qemu-init.sh" -nt "$ROOTFS" ]; then
	tmp_root="$(mktemp -d "$BUILD_DIR/rv8-rootfs.XXXXXX")"
	trap 'find "$tmp_root" -depth -delete 2>/dev/null || true' EXIT
	(
		cd "$tmp_root"
		cpio -idm < "$BASE_ROOTFS" 2>/dev/null
		install -m 0755 "$SCRIPT_DIR/rv8-qemu-init.sh" init
		find . -print | cpio -o -H newc 2>/dev/null > "$ROOTFS"
	)
	find "$tmp_root" -depth -delete
	trap - EXIT
fi

QEMU="$(zion_resolve_outer_qemu "$ZION_DIR")"
"$QEMU" -machine virt,dumpdtb="$BASE_DTB" -cpu rv64,sstc=false -m 4G -smp 4 \
	-display none >/dev/null 2>&1
install -m 0644 "$BASE_DTB" "$DTB"
fdtput -c "$DTB" /reserved-memory
fdtput -t i "$DTB" /reserved-memory '#address-cells' 2
fdtput -t i "$DTB" /reserved-memory '#size-cells' 2
fdtput -t x "$DTB" /reserved-memory ranges
fdtput -c "$DTB" /reserved-memory/zion-core@f0000000
fdtput -t s "$DTB" /reserved-memory/zion-core@f0000000 compatible \
	'zion,trusted-memory'
fdtput -t x "$DTB" /reserved-memory/zion-core@f0000000 reg \
	0 0xf0000000 0 0x10000000
fdtput -t x "$DTB" /reserved-memory/zion-core@f0000000 no-map

sha256sum "$QEMU" "$SM_FW" "$KERNEL" "$ROOTFS" "$DTB" \
	"$SHARED_DIR/zion-driver.ko" "$SHARED_DIR/test-runner" \
	"$SHARED_DIR/zion-driver.ko.build-manifest" \
	"$SHARED_DIR/eyrie-rv8" "$SHARED_DIR/loader-rv8.bin" \
	"${PROGRAMS[@]/#/$SHARED_DIR/rv8/}" > "$ARTIFACT_MANIFEST"
manifest_digest="$(sha256sum "$ARTIFACT_MANIFEST" | awk '{print $1}')"
printf '[ZION EVIDENCE] artifact manifest sha256: %s\n' "$manifest_digest" > "$LOG"

echo "[RV8 QEMU] Starting native and enclave RV8 suite"
echo "[RV8 QEMU] Log: $LOG"
set +e
timeout --foreground "${RV8_QEMU_TIMEOUT:-3600}s" "$QEMU" \
	-machine virt -cpu rv64,sstc=false \
	-m 4G -smp 4 \
	-bios "$SM_FW" \
	-kernel "$KERNEL" \
	-initrd "$ROOTFS" \
	-dtb "$DTB" \
	-append "console=ttyS0 root=/dev/ram rw $RV8_QEMU_CMA_ARG zion_native_enclave_cpu=$RV8_QEMU_ENCLAVE_CPU" \
	-fsdev local,id=shared,path="$SHARED_DIR",security_model=none \
	-device virtio-9p-device,fsdev=shared,mount_tag=hostshare \
	-nographic -no-reboot 2>&1 | tee -a "$LOG"
qemu_rc=${PIPESTATUS[0]}
set -e

[ "$qemu_rc" -eq 0 ] || fail "QEMU failed or timed out (exit=$qemu_rc)"
grep -Fq '[RV8-QEMU] PASS: native 8/8, enclave 8/8, miniz all-hart 4/4' "$LOG" ||
	fail "the complete PASS marker is missing from $LOG"
[ "$(grep -Fc '[RV8] PASS: native/' "$LOG")" -eq 8 ] ||
	fail "native PASS count is not 8"
[ "$(grep -Ec '\[RV8\] PASS: enclave-cpu[0-3]/' "$LOG")" -eq 12 ] ||
	fail "pinned enclave PASS count is not 12"
if grep -Eqi '\[RV8(-QEMU)?\] FAIL:|Kernel panic|Oops:|rcu.*stall|soft lockup' "$LOG"; then
	fail "failure, kernel crash, or scheduler-stall marker found in $LOG"
fi
retired_name='key''stone'
if grep -a -i -q "$retired_name" "$LOG"; then
	fail "retired product name found in $LOG"
fi
sha256sum -c "$ARTIFACT_MANIFEST" >/dev/null ||
	fail "executed artifacts changed after the run"

echo "[RV8 QEMU] PASS: native 8/8, enclave 8/8, miniz all-hart 4/4"
