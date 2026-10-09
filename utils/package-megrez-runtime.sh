#!/bin/bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
if [[ ${1:-} == --help ]]; then
    echo "Usage: MEGREZ_RUNTIME_INPUT=VALIDATED_RELEASE bash $0 [NEW_RELEASE_DIRECTORY]"
    echo 'Optional overrides: MEGREZ_FIRMWARE_BUILD, MEGREZ_HOST_KERNEL, MEGREZ_ASTER_KERNEL,'
    echo 'MEGREZ_LINUX_INITRD, MEGREZ_ASTER_INITRD, MEGREZ_TVM_DRIVER, MEGREZ_TVM_CONTROL'
    exit 0
fi
INPUT=${MEGREZ_RUNTIME_INPUT:-}
if [[ -n "$INPUT" ]]; then
    INPUT=$(cd "$INPUT" && pwd)
    [[ -f "$INPUT/SHA256SUMS" ]] || { echo 'Missing input package checksums' >&2; exit 1; }
    (cd "$INPUT" && sha256sum -c SHA256SUMS)
fi
BASE=${MEGREZ_BASE_RELEASE:-$ROOT/output/megrez-release-20260909}
QEMU=${MEGREZ_QEMU_BINARY:-$ROOT/output/megrez-qemu-fix-20260916/qemu-system-riscv64}
HOST_INITRD=${MEGREZ_HOST_INITRD:-$ROOT/virt/initrd.img-6.6.87-win2030}
HOST_KERNEL=${MEGREZ_HOST_KERNEL:-$BASE/kernels/host_kernel_image}
FIRMWARE=${MEGREZ_FIRMWARE_BINARY:-$BASE/boot/bootloader_secboot_ddr5_milkv_megrez.bin}
PUBLIC_KEY=${MEGREZ_DEVICE_PUBLIC_KEY:-$BASE/keys/zion-test-device-public-key.bin}
FIRMWARE_BUILD=${MEGREZ_FIRMWARE_BUILD:-}
GUEST_KERNEL=$BASE/kernels/guest_kernel_image
TVM_DRIVER=$BASE/drivers/tvm-driver.ko
TVM_CONTROL=$BASE/drivers/tvm-control
LINUX_INITRD=$ROOT/output/zion-test-initramfs/initrd-linux.img
ASTER_TEST=$ROOT/patch/zion-test-scripts/zion-tests/asterinas-test
ASTER_KERNEL=${MEGREZ_ASTER_KERNEL:-$ASTER_TEST/asterinas_kernel}
ASTER_INITRD=${MEGREZ_ASTER_INITRD:-$ASTER_TEST/asterinas_initramfs.cpio.gz}
if [[ -n "$INPUT" ]]; then
    INPUT_RUNTIME=$INPUT/root-partition/root/zion-tests
    HOST_KERNEL=${MEGREZ_HOST_KERNEL:-$INPUT/boot-partition/vmlinuz-6.6.87-win2030}
    HOST_INITRD=${MEGREZ_HOST_INITRD:-$INPUT/boot-partition/initrd.img-6.6.87-win2030}
    FIRMWARE=${MEGREZ_FIRMWARE_BINARY:-$INPUT/boot-partition/bootloader_secboot_ddr5_milkv_megrez.bin}
    PUBLIC_KEY=${MEGREZ_DEVICE_PUBLIC_KEY:-$INPUT/identity/zion-test-device-public-key.bin}
    QEMU=${MEGREZ_QEMU_BINARY:-$INPUT_RUNTIME/qemu-system-riscv64}
    GUEST_KERNEL=$INPUT_RUNTIME/guest_kernel_image
    TVM_DRIVER=$INPUT_RUNTIME/tvm-driver.ko
    TVM_CONTROL=$INPUT_RUNTIME/tvm-control
    if [[ -f "$INPUT_RUNTIME/initrd-linux.img" ]]; then
        LINUX_INITRD=$INPUT_RUNTIME/initrd-linux.img
        ASTER_KERNEL=${MEGREZ_ASTER_KERNEL:-$INPUT_RUNTIME/asterinas_kernel}
        ASTER_INITRD=${MEGREZ_ASTER_INITRD:-$INPUT_RUNTIME/initrd-asterinas.img}
    fi
fi
TVM_DRIVER=${MEGREZ_TVM_DRIVER:-$TVM_DRIVER}
TVM_CONTROL=${MEGREZ_TVM_CONTROL:-$TVM_CONTROL}
LINUX_INITRD=${MEGREZ_LINUX_INITRD:-$LINUX_INITRD}
if [[ -n "$FIRMWARE_BUILD" ]]; then
    FIRMWARE=$FIRMWARE_BUILD/firmware/bootloader_secboot_ddr5_milkv_megrez.bin
    PUBLIC_KEY=$FIRMWARE_BUILD/identity/zion-test-device-public-key.bin
fi
DEST=${1:-$ROOT/output/zion-megrez-release-$(date +%Y%m%d)}
# An existing package must never be silently merged with stale build artifacts.
[[ ! -e "$DEST" ]] || { echo "Destination already exists: $DEST" >&2; exit 1; }
for file in "$FIRMWARE" "$PUBLIC_KEY" \
    "$HOST_KERNEL" "$GUEST_KERNEL" "$TVM_DRIVER" "$TVM_CONTROL" \
    "$LINUX_INITRD" "$ASTER_KERNEL" "$ASTER_INITRD" "$QEMU" "$HOST_INITRD"; do
    [[ -f "$file" ]] || { echo "Missing runtime input: $file" >&2; exit 1; }
done
grep -aFq '[ZION HOST TEST] controlled-tamper' "$HOST_KERNEL" || {
    echo 'Full Zion test release requires the new Host kernel; set MEGREZ_RUNTIME_INPUT or MEGREZ_HOST_KERNEL.' >&2
    exit 1
}
grep -aFq '[ZION VCPU ALERT]' "$FIRMWARE" || {
    echo 'Full three-test release requires the new Zion Megrez firmware.' >&2
    exit 1
}
grep -aFq '[SM] private split block failed:' "$FIRMWARE" || {
    echo 'Asterinas release requires the Zion split private-block firmware fix.' >&2
    exit 1
}
grep -aFq '[SM] fatal: unhandled monitor trap: hart=' "$FIRMWARE" || {
    echo 'Release firmware is missing detailed Zion trap diagnostics.' >&2
    exit 1
}
install -d "$DEST/boot-partition" "$DEST/root-partition/root/zion-tests" "$DEST/identity" "$DEST/docs"
RUNTIME=$DEST/root-partition/root/zion-tests
install -m 644 "$FIRMWARE" "$DEST/boot-partition/bootloader_secboot_ddr5_milkv_megrez.bin"
install -m 644 "$HOST_KERNEL" "$DEST/boot-partition/vmlinuz-6.6.87-win2030"
install -m 644 "$HOST_INITRD" "$DEST/boot-partition/initrd.img-6.6.87-win2030"
install -m 644 "$GUEST_KERNEL" "$RUNTIME/guest_kernel_image"
install -m 644 "$TVM_DRIVER" "$RUNTIME/tvm-driver.ko"
install -m 644 "$LINUX_INITRD" "$RUNTIME/initrd-linux.img"
install -m 644 "$ASTER_KERNEL" "$RUNTIME/asterinas_kernel"
install -m 644 "$ASTER_INITRD" "$RUNTIME/initrd-asterinas.img"
install -m 755 "$TVM_CONTROL" "$RUNTIME/tvm-control"
install -m 755 "$QEMU" "$RUNTIME/qemu-system-riscv64"
install -m 755 "$ROOT/utils/env/zion-runtime.sh" "$RUNTIME/"
install -m 644 "$ROOT/utils/env/megrez-runtime.conf" "$RUNTIME/"
install -m 644 "$ROOT/utils/env/megrez-release-quick-test-zh.md" "$RUNTIME/QUICK_TEST_ZH.md"
install -m 644 "$PUBLIC_KEY" "$DEST/identity/zion-test-device-public-key.bin"
install -m 644 "$ROOT/utils/env/megrez-release-README-zh.md" "$DEST/README.md"
install -m 644 "$ROOT/utils/env/megrez-release-quick-test-zh.md" "$DEST/QUICK_TEST_ZH.md"
install -m 644 "$ROOT/utils/env/megrez-release-firmware-zh.md" "$DEST/boot-partition/MEGREZ_DEPLOY_TEST_ZH.md"
install -m 644 "$ROOT/utils/env/megrez-release-deployment-zh.md" "$DEST/docs/DEPLOYMENT_ZH.md"
install -m 644 "$ROOT/utils/env/megrez-release-tests-zh.md" "$DEST/docs/TESTS_ZH.md"
install -m 644 "$ROOT/utils/env/megrez-release-evidence-zh.md" "$DEST/docs/EVIDENCE_ZH.md"
install -m 755 "$ROOT/utils/env/install-megrez-release.sh" "$DEST/install-to-device.sh"
(cd "$DEST" && find . -type f ! -name SHA256SUMS -print0 | sort -z | xargs -0 sha256sum > SHA256SUMS)
echo "Zion Megrez full runtime release: $DEST"
