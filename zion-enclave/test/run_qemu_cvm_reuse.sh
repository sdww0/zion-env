#!/usr/bin/env bash
# Verify that one unified driver/core reservation serves enclaves and CVMs.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ZION_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ZION_DIR/build}"
source "$ZION_DIR/scripts/zion_paths.sh"

HOST_BUILD="${HOST_BUILD:-$BUILD_DIR/cvm-host-linux-6.6}"
HOST_IMAGE="${HOST_IMAGE:-$HOST_BUILD/arch/riscv/boot/Image}"
HOST_BASE="${HOST_BASE:-$BUILD_DIR/cvm-host-rootfs}"
SM_FW="${SM_FW:-$BUILD_DIR/opensbi/platform/generic/firmware/fw_dynamic.bin}"
GUEST_IMAGE="${GUEST_IMAGE:-$HOST_IMAGE}"
GUEST_INITRD="${GUEST_INITRD:-$BUILD_DIR/cvm-rv8/guest-rv8.cpio}"
MEGREZ_CVM="${MEGREZ_CVM:-$BUILD_DIR/megrez-test-assets/root/cvm}"
UNIFIED_DRIVER="${UNIFIED_DRIVER:-$BUILD_DIR/megrez-modules/zion-driver.ko}"
ENCLAVE_ROOT="${ENCLAVE_ROOT:-$BUILD_DIR/megrez-test-assets/root/opt/zion-tests/enclave}"
HOST_INITRD="$BUILD_DIR/cvm-reuse/host-reuse.cpio"
LOG="${CVM_REUSE_LOG:-$BUILD_DIR/cvm-reuse.log}"
MANIFEST="$LOG.artifacts.sha256"

fail()
{
	echo "[CVM REUSE] FAIL: $*" >&2
	exit 1
}

for command_name in cpio modinfo sha256sum timeout; do
	command -v "$command_name" >/dev/null || fail "missing command: $command_name"
done
for path in "$HOST_IMAGE" "$SM_FW" "$GUEST_IMAGE" "$GUEST_INITRD" \
	"$HOST_BASE/cvm/qemu-system-riscv64" "$UNIFIED_DRIVER" \
	"$MEGREZ_CVM/tvm-control" "$ENCLAVE_ROOT/hello-runner" \
	"$ENCLAVE_ROOT/hello" "$ENCLAVE_ROOT/eyrie-rt" \
	"$ENCLAVE_ROOT/loader.bin" "$SCRIPT_DIR/cvm-reuse-host-init.sh"; do
	[ -s "$path" ] || fail "missing artifact: $path"
done
[ "$(modinfo -F vermagic "$UNIFIED_DRIVER" | awk '{print $1}')" = \
	"$(cat "$HOST_BUILD/include/config/kernel.release")" ] ||
	fail "unified Zion driver vermagic does not match host kernel"

mkdir -p "$BUILD_DIR/cvm-reuse"
host_tmp=$(mktemp -d "$BUILD_DIR/cvm-reuse-host.XXXXXX")
cleanup()
{
	find "$host_tmp" -depth -delete 2>/dev/null || true
}
trap cleanup EXIT

cp -a "$HOST_BASE/." "$host_tmp/"
install -m 0755 "$SCRIPT_DIR/cvm-reuse-host-init.sh" "$host_tmp/init"
install -m 0644 "$UNIFIED_DRIVER" "$host_tmp/cvm/zion-driver.ko"
install -m 0755 "$MEGREZ_CVM/tvm-control" "$host_tmp/cvm/tvm-control"
install -d "$host_tmp/cvm/enclave"
install -m 0755 "$ENCLAVE_ROOT/hello-runner" "$host_tmp/cvm/enclave/hello-runner"
install -m 0755 "$ENCLAVE_ROOT/hello" "$host_tmp/cvm/enclave/hello"
install -m 0755 "$ENCLAVE_ROOT/eyrie-rt" "$host_tmp/cvm/enclave/eyrie-rt"
install -m 0644 "$ENCLAVE_ROOT/loader.bin" "$host_tmp/cvm/enclave/loader.bin"
install -m 0644 "$GUEST_IMAGE" "$host_tmp/cvm/guest_kernel_image"
install -m 0644 "$GUEST_INITRD" "$host_tmp/cvm/guest_rv8_initrd.img"
(
	cd "$host_tmp"
	find . -print0 | cpio --null -o --format=newc >"$HOST_INITRD" 2>/dev/null
)

QEMU="$(zion_resolve_outer_qemu "$ZION_DIR")"
sha256sum "$QEMU" "$SM_FW" "$HOST_IMAGE" "$HOST_INITRD" \
	"$GUEST_IMAGE" "$GUEST_INITRD" "$UNIFIED_DRIVER" \
	"$MEGREZ_CVM/tvm-control" "$HOST_BASE/cvm/qemu-system-riscv64" \
	"$ENCLAVE_ROOT/hello-runner" "$ENCLAVE_ROOT/hello" \
	"$ENCLAVE_ROOT/eyrie-rt" "$ENCLAVE_ROOT/loader.bin" \
	"$SCRIPT_DIR/cvm-reuse-host-init.sh" >"$MANIFEST"
manifest_digest=$(sha256sum "$MANIFEST" | awk '{print $1}')
printf '[ZION EVIDENCE] artifact manifest sha256: %s\n' \
	"$manifest_digest" >"$LOG"

echo "[CVM REUSE] Starting sequential-CVM QEMU regression"
echo "[CVM REUSE] Log: $LOG"
set +e
timeout --foreground "${CVM_REUSE_TIMEOUT:-7200}s" "$QEMU" \
	-machine virt -cpu rv64,sstc=false \
	-m "${CVM_REUSE_HOST_MEMORY:-4G}" -smp 2 \
	-bios "$SM_FW" -kernel "$HOST_IMAGE" -initrd "$HOST_INITRD" \
	-append 'console=ttyS0 rw cma=1G' \
	-nographic -no-reboot 2>&1 | tee -a "$LOG"
qemu_rc=${PIPESTATUS[0]}
set -e

[ "$qemu_rc" -eq 0 ] || fail "outer QEMU failed or timed out (exit=$qemu_rc)"
grep -Fq '[CVM-REUSE-QEMU] PASS: one unified driver served native enclave plus two CVM/enclave/RV8 runs' \
	"$LOG" || fail "final reuse marker is absent"
grep -Fq '[CVM-REUSE-QEMU] PASS: native enclave destroyed; unified pool fully idle' \
	"$LOG" || fail "native enclave unified-pool marker is absent"
[ "$(grep -Fc '[CVM-REUSE-QEMU] PASS: iteration ' "$LOG")" -eq 2 ] ||
	fail "two destroyed/idle iteration markers are absent"
[ "$(grep -Fc '[CVM-RV8] PASS: enclave 8/8' "$LOG")" -eq 2 ] ||
	fail "two nested RV8 8/8 markers are absent"
[ "$(grep -Fc '[CVM-RV8] PASS: enclave/' "$LOG")" -eq 16 ] ||
	fail "nested enclave PASS count is not 16"
if grep -Eq '\[CVM-REUSE-QEMU\] FAIL:|\[CVM-RV8\] FAIL:|Kernel panic|Oops:' \
	"$LOG"; then
	fail "failure or kernel crash marker found in log"
fi
sha256sum -c "$MANIFEST" >/dev/null ||
	fail "executed artifacts changed after the run"
echo "[CVM REUSE] PASS: one unified driver served native enclave and two clean CVM runs"
