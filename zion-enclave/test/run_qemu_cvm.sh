#!/usr/bin/env bash
# Boot the recovered two-level CVM stack under the outer system QEMU.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ZION_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ZION_DIR/build}"
source "$ZION_DIR/scripts/zion_paths.sh"
LOG_VERIFIER="$ZION_DIR/scripts/verify_runtime_log.sh"

QEMU="$(zion_resolve_outer_qemu "$ZION_DIR")"
SM_FW="${SM_FW:-$BUILD_DIR/opensbi/platform/generic/firmware/fw_dynamic.bin}"
HOST_IMAGE="${HOST_IMAGE:-$BUILD_DIR/cvm-host-linux-6.6/arch/riscv/boot/Image}"
HOST_INITRD="${HOST_INITRD:-$BUILD_DIR/cvm-host-rootfs.cpio}"
HOST_MEMORY="${HOST_MEMORY:-4G}"
HOST_CPUS="${HOST_CPUS:-2}"
CVM_HOST_CMA="${CVM_HOST_CMA:-1G}"
CVM_GUEST_COUNT="${CVM_GUEST_COUNT:-2}"
CVM_TEST_MODE="${CVM_TEST_MODE:-full}"
MODE="${1:-auto}"

case "$MODE" in
    auto) HOST_APPEND="console=ttyS0 rw cma=$CVM_HOST_CMA cvm_guest_count=$CVM_GUEST_COUNT cvm_test_mode=$CVM_TEST_MODE cvm-auto" ;;
    manual) HOST_APPEND="console=ttyS0 rw cma=$CVM_HOST_CMA cvm_guest_count=$CVM_GUEST_COUNT cvm_test_mode=$CVM_TEST_MODE" ;;
    *)
        echo "[FAIL] unknown mode '$MODE' (use auto or manual)" >&2
        exit 1
        ;;
esac

for file in "$QEMU" "$SM_FW" "$HOST_IMAGE" "$HOST_INITRD"; do
    if [ ! -f "$file" ]; then
        echo "[FAIL] missing: $file" >&2
        exit 1
    fi
done

echo "[CVM] outer QEMU: $QEMU"
echo "[CVM] SM:         $SM_FW"
echo "[CVM] host Image: $HOST_IMAGE"
echo "[CVM] host initrd:$HOST_INITRD"
echo "[CVM] mode:       $MODE"
echo "[CVM] test mode:  $CVM_TEST_MODE"
if [ "$MODE" = manual ]; then
    echo "[CVM] exit with Ctrl-A X when finished"
fi

RUN_LOG="${CVM_RUN_LOG:-$BUILD_DIR/cvm-qemu-last.log}"
ARTIFACT_MANIFEST="${CVM_ARTIFACT_MANIFEST:-$RUN_LOG.artifacts.sha256}"
command -v sha256sum >/dev/null || {
    echo "[FAIL] sha256sum is required" >&2
    exit 1
}
if [ ! -x "$LOG_VERIFIER" ]; then
    echo "[FAIL] missing runtime log verifier: $LOG_VERIFIER" >&2
    exit 1
fi
sha256sum "$QEMU" "$SM_FW" "$HOST_IMAGE" "$HOST_INITRD" \
    > "$ARTIFACT_MANIFEST"
manifest_digest="$(sha256sum "$ARTIFACT_MANIFEST" | awk '{print $1}')"
printf '[ZION EVIDENCE] artifact manifest sha256: %s\n' \
    "$manifest_digest" > "$RUN_LOG"
set +e
"$QEMU" \
    -machine virt \
    -cpu rv64,sstc=false \
    -m "$HOST_MEMORY" \
    -smp "$HOST_CPUS" \
    -bios "$SM_FW" \
    -kernel "$HOST_IMAGE" \
    -initrd "$HOST_INITRD" \
    -append "$HOST_APPEND" \
    -nographic \
    -no-reboot 2>&1 | tee -a "$RUN_LOG"
qemu_rc=${PIPESTATUS[0]}
set -e

if [ "$qemu_rc" -ne 0 ]; then
    echo "[FAIL] outer QEMU exited with status $qemu_rc" >&2
    exit "$qemu_rc"
fi

"$LOG_VERIFIER" cvm "$RUN_LOG" "$ARTIFACT_MANIFEST"

echo "[CVM] PASS: complete QEMU log validation succeeded"
