#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
if [[ ${1:-} == --help ]]; then
    echo "Usage: ZION_RUNTIME_INPUT=VALIDATED_RELEASE bash $0 [NEW_OUTPUT_DIRECTORY]"
    echo 'Optional env: ASTER_INITRAMFS (tested SSH initramfs containing SQLite)'
    exit 0
fi
SOURCE=${ZION_RUNTIME_INPUT:-$ROOT/output/zion-megrez-release-20260917}
ASTER=${ASTER_INITRAMFS:-$ROOT/patch/zion-test-scripts/zion-tests/asterinas-test/asterinas_initramfs.cpio.gz}
DEST=${1:-$ROOT/output/zion-test-initramfs}
[[ ! -e "$DEST" ]] || { printf 'Destination exists: %s\n' "$DEST" >&2; exit 1; }
[[ -f "$SOURCE/SHA256SUMS" ]] || { printf 'Missing source checksums\n' >&2; exit 1; }
SOURCE=$(realpath "$SOURCE")
ASTER=$(realpath "$ASTER")
DEST=$(realpath -m "$DEST")
(cd "$SOURCE" && sha256sum -c SHA256SUMS)
mkdir -p "$DEST/linux-root" "$DEST/aster-root"
for kind in linux aster; do
    image=$ASTER
    if [[ "$kind" = linux ]]; then
        image=$SOURCE/root-partition/root/zion-tests/initrd-linux.img
        [[ -f "$image" ]] || image=$SOURCE/root-partition/root/zion-tests/initrd-enclave.img
    fi
    (cd "$DEST/$kind-root" && gzip -dc "$image" | cpio -idm --quiet --no-absolute-filenames)
done
SQLITE=$DEST/aster-root/benchmark/bin/sqlite-speedtest1
[[ -x "$SQLITE" ]] || { printf 'Missing tested sqlite-speedtest1\n' >&2; exit 1; }
mkdir -p "$DEST/linux-root/benchmark/bin"
install -m 755 "$SQLITE" "$DEST/linux-root/benchmark/bin/sqlite-speedtest1"
for kind in linux aster; do
    install -m 755 "$ROOT/utils/env/zion-sqlite-boot.sh" "$DEST/$kind-root/etc/zion-sqlite-boot.sh"
    name=$kind
    [[ $kind != aster ]] || name=asterinas
    (cd "$DEST/$kind-root" && find . -print0 | cpio --null -o -H newc --owner=0:0 --quiet | gzip -9 > "../initrd-$name.img")
done
printf 'Built Linux SSH/enclave/SQLite and Asterinas SSH/SQLite initramfs: %s\n' "$DEST"
