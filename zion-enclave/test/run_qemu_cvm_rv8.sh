#!/usr/bin/env bash
# Build and run Host -> CVM -> Zion enclave -> pinned RV8 under QEMU.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ZION_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ZION_DIR/build}"
source "$ZION_DIR/scripts/zion_paths.sh"

HOST_BUILD="${HOST_BUILD:-$BUILD_DIR/cvm-host-linux-6.6}"
HOST_IMAGE="${HOST_IMAGE:-$HOST_BUILD/arch/riscv/boot/Image}"
HOST_BASE="${HOST_BASE:-$BUILD_DIR/cvm-host-rootfs}"
GUEST_BASE="${GUEST_BASE:-$BUILD_DIR/cvm-guest-smoke-rootfs}"
GUEST_IMAGE="${GUEST_IMAGE:-$HOST_IMAGE}"
SM_FW="${SM_FW:-$BUILD_DIR/opensbi/platform/generic/firmware/fw_dynamic.bin}"
RV8_SHARED="${RV8_SHARED:-$BUILD_DIR/rv8-qemu-shared}"
DRIVER="$BUILD_DIR/cvm-rv8/zion-driver.ko"
GUEST_INITRD="$BUILD_DIR/cvm-rv8/guest-rv8.cpio"
HOST_INITRD="$BUILD_DIR/cvm-rv8/host-rv8.cpio"
LOG="${CVM_RV8_LOG:-$BUILD_DIR/cvm-rv8.log}"
ARTIFACT_MANIFEST="$LOG.artifacts.sha256"
PROGRAMS=(aes bigint dhrystone miniz norx primes qsort sha512)

fail() {
	echo "[CVM RV8] FAIL: $*" >&2
	exit 1
}

for command_name in cpio make modinfo sha256sum strings timeout; do
	command -v "$command_name" >/dev/null || fail "missing command: $command_name"
done
for path in "$HOST_IMAGE" "$GUEST_IMAGE" "$SM_FW" \
	"$HOST_BASE/cvm/qemu-system-riscv64" "$HOST_BASE/cvm/tvm-driver.ko" \
	"$HOST_BASE/cvm/tvm-control" "$GUEST_BASE/usr/bin/busybox" \
	"$RV8_SHARED/test-runner" "$RV8_SHARED/eyrie-rv8" \
	"$RV8_SHARED/loader-rv8.bin"; do
	[ -s "$path" ] || fail "missing artifact: $path"
done
for program in "${PROGRAMS[@]}"; do
	[ -s "$RV8_SHARED/rv8/$program" ] || fail "missing RV8 binary: $program"
done

mkdir -p "$BUILD_DIR/cvm-rv8"
CROSS_COMPILE="$(zion_resolve_cross_compile)"
if [ -f "$HOST_BUILD/vmlinux.symvers" ]; then
	cp "$HOST_BUILD/vmlinux.symvers" "$HOST_BUILD/Module.symvers"
fi
echo "[CVM RV8] Building CVM-compatible Zion driver"
make -C "$HOST_BUILD" -j"${JOBS:-$(nproc)}" \
	ARCH=riscv CROSS_COMPILE="$CROSS_COMPILE" \
	M="$ZION_DIR/driver/kernel" ZION_SDK_DIR="$ZION_DIR/sdk" modules
install -m 0644 "$ZION_DIR/driver/kernel/zion-driver.ko" "$DRIVER"
[ "$(modinfo -F vermagic "$DRIVER" | awk '{print $1}')" = \
	"$(cat "$HOST_BUILD/include/config/kernel.release")" ] ||
	fail "Zion driver vermagic does not match guest kernel"
grep -F 'nested CVM mode; using parent-owned SM pool' \
	< <(strings "$DRIVER") >/dev/null ||
	fail "Zion driver lacks nested CVM mode"

guest_tmp=$(mktemp -d "$BUILD_DIR/cvm-rv8-guest.XXXXXX")
host_tmp=$(mktemp -d "$BUILD_DIR/cvm-rv8-host.XXXXXX")
cleanup() {
	find "$guest_tmp" -depth -delete 2>/dev/null || true
	find "$host_tmp" -depth -delete 2>/dev/null || true
}
trap cleanup EXIT

echo "[CVM RV8] Packing minimal RV8 guest initramfs"
cp -a "$GUEST_BASE/." "$guest_tmp/"
install -m 0755 "$SCRIPT_DIR/cvm-rv8-init.sh" "$guest_tmp/init"
install -d "$guest_tmp/rv8/bin" "$guest_tmp/tmp"
install -m 0644 "$DRIVER" "$guest_tmp/rv8/zion-driver.ko"
install -m 0755 "$RV8_SHARED/test-runner" "$guest_tmp/rv8/test-runner"
install -m 0755 "$RV8_SHARED/eyrie-rv8" "$guest_tmp/rv8/eyrie-rv8"
install -m 0644 "$RV8_SHARED/loader-rv8.bin" "$guest_tmp/rv8/loader-rv8.bin"
for program in "${PROGRAMS[@]}"; do
	install -m 0755 "$RV8_SHARED/rv8/$program" "$guest_tmp/rv8/bin/$program"
done
(
	cd "$guest_tmp/rv8"
	sha256sum zion-driver.ko test-runner eyrie-rv8 loader-rv8.bin \
		bin/aes bin/bigint bin/dhrystone bin/miniz bin/norx \
		bin/primes bin/qsort bin/sha512 > MANIFEST.sha256
	sha256sum -c MANIFEST.sha256 >/dev/null
)
(
	cd "$guest_tmp"
	find . -print0 | cpio --null -o --format=newc > "$GUEST_INITRD" 2>/dev/null
)

echo "[CVM RV8] Packing dedicated outer host initramfs"
cp -a "$HOST_BASE/." "$host_tmp/"
install -m 0755 "$SCRIPT_DIR/cvm-rv8-host-init.sh" "$host_tmp/init"
install -m 0644 "$GUEST_IMAGE" "$host_tmp/cvm/guest_kernel_image"
install -m 0644 "$GUEST_INITRD" "$host_tmp/cvm/guest_rv8_initrd.img"
(
	cd "$host_tmp"
	find . -print0 | cpio --null -o --format=newc > "$HOST_INITRD" 2>/dev/null
)

QEMU="$(zion_resolve_outer_qemu "$ZION_DIR")"
sha256sum "$QEMU" "$SM_FW" "$HOST_IMAGE" "$HOST_INITRD" \
	"$GUEST_IMAGE" "$GUEST_INITRD" "$DRIVER" \
	"$RV8_SHARED/test-runner" "$RV8_SHARED/eyrie-rv8" \
	"$RV8_SHARED/loader-rv8.bin" \
	"${PROGRAMS[@]/#/$RV8_SHARED/rv8/}" > "$ARTIFACT_MANIFEST"
manifest_digest=$(sha256sum "$ARTIFACT_MANIFEST" | awk '{print $1}')
printf '[ZION EVIDENCE] artifact manifest sha256: %s\n' \
	"$manifest_digest" > "$LOG"

echo "[CVM RV8] Starting nested RV8 QEMU regression"
echo "[CVM RV8] Log: $LOG"
set +e
timeout --foreground "${CVM_RV8_TIMEOUT:-5400}s" "$QEMU" \
	-machine virt -cpu rv64,sstc=false \
	-m "${CVM_RV8_HOST_MEMORY:-4G}" -smp 4 \
	-bios "$SM_FW" -kernel "$HOST_IMAGE" -initrd "$HOST_INITRD" \
	-append 'console=ttyS0 rw cma=1G' \
	-nographic -no-reboot 2>&1 | tee -a "$LOG"
qemu_rc=${PIPESTATUS[0]}
set -e

[ "$qemu_rc" -eq 0 ] || fail "outer QEMU failed or timed out (exit=$qemu_rc)"
grep -Fq '[CVM-RV8-QEMU] PASS: Host -> CVM -> enclave -> RV8 8/8' "$LOG" ||
	fail "complete Host -> CVM -> enclave -> RV8 marker missing"
grep -Fq '[CVM-RV8] PASS: enclave 8/8' "$LOG" ||
	fail "nested guest 8/8 marker missing"
[ "$(grep -Fc '[CVM-RV8] PASS: enclave/' "$LOG")" -eq 8 ] ||
	fail "nested enclave PASS count is not 8"
if grep -Eqi '\[CVM-RV8(-QEMU)?\] FAIL:|Kernel panic|Oops:|rcu.*stall|soft lockup' "$LOG"; then
	fail "failure, lockup, or RCU stall marker found in log"
fi
retired_name='key''stone'
if grep -a -i -q "$retired_name" "$LOG"; then
	fail "retired product name found in log"
fi
sha256sum -c "$ARTIFACT_MANIFEST" >/dev/null ||
	fail "executed artifacts changed after the run"
echo "[CVM RV8] PASS: Host -> CVM -> enclave -> RV8 8/8"
