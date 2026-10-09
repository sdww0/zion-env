#!/bin/bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
if [[ ${1:-} == --help ]]; then
    echo "Usage: bash $0 NEW_BUILD_DIRECTORY"
    echo 'Optional env: MEGREZ_BASE_RELEASE (U-Boot/DDR inputs), ZION_KERNEL_IMAGE'
    exit 0
fi
BASE=${MEGREZ_BASE_RELEASE:-$ROOT/output/megrez-release-20260909}
BUILD=${1:?Usage: build-megrez-release-firmware.sh NEW_BUILD_DIRECTORY}
IMAGE=${ZION_KERNEL_IMAGE:-localhost/zion-kernel:0.1.0}
[[ ! -e "$BUILD" ]] || { echo "Build directory exists: $BUILD" >&2; exit 1; }
mkdir -p "$BUILD"
BUILD=$(cd "$BUILD" && pwd)
case "$BUILD" in
    "$ROOT"/*) CONTAINER_BUILD=/root/${BUILD#"$ROOT"/} ;;
    *) echo 'Build directory must be under the workspace' >&2; exit 1 ;;
esac
mkdir -p "$BUILD/firmware" "$BUILD/sign" "$BUILD/identity"
install -m 644 "$BASE/boot/u-boot-megrez.bin" "$BASE/boot/u-boot-megrez.dtb" "$BUILD/firmware/"
# Delete the ephemeral private-key source even when compilation fails.
trap 'rm -f "$BUILD/test-key.h"' EXIT
bash "$ROOT/utils/generate-zion-test-key.sh" "$BUILD/test-key.h" \
    "$BUILD/identity/zion-test-device-public-key.bin"
{
    printf 'OpenSBI HEAD: '
    git -C "$ROOT/opensbi" rev-parse HEAD
    printf 'Base U-Boot/DDR inputs: %s\n' "$BASE"
    printf 'Container: %s\n' "$IMAGE"
    podman image inspect --format '{{.Id}}' "$IMAGE"
    printf 'Options: PLATFORM=generic FW_PAYLOAD=y ZION_INSECURE_TEST_KEYS=1 '
    printf 'ZION_DYNAMIC_PMP=1 ZION_USE_ENVCFG=0\n'
    printf 'OpenSBI root-domain PMP is retained; Zion inserts the CVM pool dynamically.\n'
} > "$BUILD/BUILD_INPUTS.txt"
git -C "$ROOT/opensbi" diff > "$BUILD/opensbi-source.diff"
podman run --rm --platform linux/amd64 --volume "$ROOT:/root" \
    --workdir /root/opensbi "$IMAGE" \
    make O="$CONTAINER_BUILD/opensbi" PLATFORM=generic \
    CROSS_COMPILE=riscv64-linux-gnu- FW_PAYLOAD=y \
    FW_FDT_PATH="$CONTAINER_BUILD/firmware/u-boot-megrez.dtb" \
    FW_PAYLOAD_PATH="$CONTAINER_BUILD/firmware/u-boot-megrez.bin" \
    ZION_INSECURE_TEST_KEYS=1 ZION_TEST_KEY_HEADER="$CONTAINER_BUILD/test-key.h" \
    ZION_DYNAMIC_PMP=1 ZION_USE_ENVCFG=0 \
    -j8 > "$BUILD/opensbi-build.log" 2>&1
install -m 644 "$BUILD/opensbi/platform/generic/firmware/fw_payload.bin" "$BUILD/sign/fw_payload.bin"
install -m 644 "$ROOT/opensbi/sign/preload/sys_init.bin" \
    "$ROOT/opensbi/sign/preload/ddr_fw.bin" "$BUILD/sign/"
# Work on a private signing copy; never rewrite the source preload directory.
sed "s|HOLDER|$CONTAINER_BUILD/sign|g" "$ROOT/opensbi/sign/preload/bootchain.cfg" \
    > "$BUILD/sign/bootchain.cfg"
podman run --rm --platform linux/amd64 --volume "$ROOT:/root" \
    --workdir "$CONTAINER_BUILD/sign" "$IMAGE" \
    /root/opensbi/sign/nsign "$CONTAINER_BUILD/sign/bootchain.cfg" \
    > "$BUILD/sign-build.log" 2>&1
test -s "$BUILD/sign/bootloader_secboot_ddr5.bin"
install -m 644 "$BUILD/sign/bootloader_secboot_ddr5.bin" \
    "$BUILD/firmware/bootloader_secboot_ddr5_milkv_megrez.bin"
install -m 644 "$ROOT/utils/env/megrez-deploy-test-zh.md" "$BUILD/firmware/MEGREZ_DEPLOY_TEST_ZH.md"
echo "Megrez firmware built: $BUILD/firmware/bootloader_secboot_ddr5_milkv_megrez.bin"
echo 'Build/sign success is not physical-board boot validation.'
