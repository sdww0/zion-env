#!/bin/bash
set -Eeuo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
BOOT_SOURCE="$SCRIPT_DIR/boot-partition"
ROOT_SOURCE="$SCRIPT_DIR/root-partition/root/zion-tests"

if [[ $EUID -ne 0 ]]; then
	echo "Run as root: sudo $0 [TARGET_DISK]" >&2
	exit 1
fi

if [[ $# -gt 1 ]]; then
	echo "Usage: sudo $0 [/dev/sdX]" >&2
	exit 2
fi

device=${1:-}
if [[ -z $device ]]; then
	read -r -p "Target disk [/dev/sdb]: " device || true
	device=${device:-/dev/sdb}
fi
[[ $device == /dev/* ]] || device="/dev/$device"

[[ -d $BOOT_SOURCE ]] || { echo "Missing $BOOT_SOURCE" >&2; exit 1; }
[[ -d $ROOT_SOURCE ]] || { echo "Missing $ROOT_SOURCE" >&2; exit 1; }
[[ -b $device ]] || { echo "Target is not a block device: $device" >&2; exit 1; }

device_type=$(lsblk -dnro TYPE "$device" 2>/dev/null || true)
[[ $device_type == disk ]] || {
	echo "Specify a whole disk, not a partition: $device (TYPE=${device_type:-unknown})" >&2
	exit 1
}

case $device in
	*[0-9]) partition_prefix="${device}p" ;;
	*) partition_prefix=$device ;;
esac
boot_device="${partition_prefix}1"
root_device="${partition_prefix}3"

[[ -b $boot_device ]] || { echo "Boot partition not found: $boot_device" >&2; exit 1; }
[[ -b $root_device ]] || { echo "Root partition not found: $root_device" >&2; exit 1; }

if findmnt -rn -S "$boot_device" >/dev/null || findmnt -rn -S "$root_device" >/dev/null; then
	echo "Target partitions are mounted; unmount $boot_device and $root_device first." >&2
	findmnt -rn -S "$boot_device" 2>/dev/null || true
	findmnt -rn -S "$root_device" 2>/dev/null || true
	exit 1
fi

shopt -s nullglob
bootloaders=("$BOOT_SOURCE"/bootloader*.bin)
shopt -u nullglob
if [[ ${#bootloaders[@]} -ne 1 ]]; then
	echo "boot-partition must contain exactly one bootloader*.bin; found ${#bootloaders[@]}." >&2
	exit 1
fi
bootloader_source=${bootloaders[0]}

if [[ -f $SCRIPT_DIR/SHA256SUMS ]]; then
	echo "Verifying release files..."
	(cd "$SCRIPT_DIR" && sha256sum -c SHA256SUMS)
fi

echo
echo "Zion release deployment:"
echo "  Disk: $device"
echo "  Boot partition: $boot_device (overwrite matching files; preserve others)"
echo "  Root partition: $root_device (replace only /root/zion-tests)"
lsblk -o NAME,SIZE,FSTYPE,LABEL,MOUNTPOINTS,MODEL "$device"
echo
confirmation=
read -r -p "Confirm the target by typing YES: " confirmation || true
[[ $confirmation == YES ]] || { echo "Cancelled."; exit 1; }

mount_base=$(mktemp -d /tmp/zion-megrez-release.XXXXXX)
boot_mount="$mount_base/boot"
root_mount="$mount_base/root"
mkdir -p "$boot_mount" "$root_mount"
boot_mounted=0
root_mounted=0

cleanup()
{
	status=$?
	trap - EXIT
	if (( root_mounted )); then
		umount "$root_mount" || status=1
	fi
	if (( boot_mounted )); then
		umount "$boot_mount" || status=1
	fi
	rmdir "$root_mount" "$boot_mount" "$mount_base" 2>/dev/null || true
	exit "$status"
}
trap cleanup EXIT

mount "$boot_device" "$boot_mount"
boot_mounted=1
mount "$root_device" "$root_mount"
root_mounted=1

findmnt -rn -M "$boot_mount" -S "$boot_device" >/dev/null || {
	echo "Boot partition mount verification failed." >&2
	exit 1
}
findmnt -rn -M "$root_mount" -S "$root_device" >/dev/null || {
	echo "Root partition mount verification failed." >&2
	exit 1
}

echo "Updating the boot partition..."
shopt -s dotglob nullglob
for source in "$BOOT_SOURCE"/*; do
	[[ $source == "$bootloader_source" ]] && continue
	cp -a -- "$source" "$boot_mount/"
done
shopt -u dotglob nullglob
install -m 0644 "$bootloader_source" "$boot_mount/bootloader.bin"

echo "Replacing /root/zion-tests on the root partition..."
mkdir -p "$root_mount/root"
rm -rf -- "$root_mount/root/zion-tests"
cp -a -- "$ROOT_SOURCE" "$root_mount/root/zion-tests"

sync
umount "$root_mount"
root_mounted=0
umount "$boot_mount"
boot_mounted=0

echo "Deployment complete:"
echo "  $boot_device: boot files updated; firmware installed as bootloader.bin"
echo "  $root_device: /root/zion-tests replaced completely"
echo "The device can now be safely removed."
