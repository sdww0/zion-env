#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
if [[ ${1:-} == --help || $# -gt 2 ]]; then
	printf 'Usage: sudo bash %s BOOT_PARTITION [BOOTLOADER_BINARY]\n' "$0"
	exit 0
fi
device=${1:?Specify a boot partition, for example /dev/sdb1}
image=${2:-$ROOT/output/bootloader_secboot_ddr5_milkv_megrez.bin}
[[ $EUID == 0 && -b $device && -s $image ]] || {
	echo 'Root permission, a block partition and a nonempty bootloader are required.' >&2
	exit 1
}
[[ $(lsblk -dnro TYPE "$device") == part ]] || {
	echo 'Specify a partition, not a whole disk.' >&2
	exit 1
}
if findmnt -rn -S "$device" >/dev/null; then
	echo "Unmount $device first." >&2
	exit 1
fi
lsblk -o NAME,SIZE,FSTYPE,LABEL,MOUNTPOINTS "$device"
read -r -p "Overwrite bootloader.bin on $device? Type YES: " answer
[[ $answer == YES ]] || exit 1
mount_dir=$(mktemp -d)
mounted=0
cleanup() {
	status=$?
	trap - EXIT
	if (( mounted )); then umount "$mount_dir" || status=1; fi
	rmdir "$mount_dir" 2>/dev/null || true
	exit "$status"
}
trap cleanup EXIT
mount "$device" "$mount_dir"
mounted=1
install -m 644 "$image" "$mount_dir/bootloader.bin"
sync
echo 'Copied bootloader.bin. Board firmware flashing is still a separate step.'

