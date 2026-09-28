#!/bin/bash
set -Eeuo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
BOOT_SOURCE="$SCRIPT_DIR/boot-partition"
ROOT_SOURCE="$SCRIPT_DIR/root-partition/root/zion-tests"

if [[ $EUID -ne 0 ]]; then
	echo "请使用 root 权限运行：sudo $0 [目标整盘设备]" >&2
	exit 1
fi

if [[ $# -gt 1 ]]; then
	echo "用法：sudo $0 [/dev/sdX]" >&2
	exit 2
fi

device=${1:-}
if [[ -z $device ]]; then
	read -r -p "目标整盘设备 [/dev/sdb]: " device || true
	device=${device:-/dev/sdb}
fi
[[ $device == /dev/* ]] || device="/dev/$device"

[[ -d $BOOT_SOURCE ]] || { echo "缺少 $BOOT_SOURCE" >&2; exit 1; }
[[ -d $ROOT_SOURCE ]] || { echo "缺少 $ROOT_SOURCE" >&2; exit 1; }
[[ -b $device ]] || { echo "目标不是块设备：$device" >&2; exit 1; }

device_type=$(lsblk -dnro TYPE "$device" 2>/dev/null || true)
[[ $device_type == disk ]] || {
	echo "请输入整盘设备而不是分区：$device (TYPE=${device_type:-unknown})" >&2
	exit 1
}

case $device in
	*[0-9]) partition_prefix="${device}p" ;;
	*) partition_prefix=$device ;;
esac
boot_device="${partition_prefix}1"
root_device="${partition_prefix}3"

[[ -b $boot_device ]] || { echo "找不到 boot 分区：$boot_device" >&2; exit 1; }
[[ -b $root_device ]] || { echo "找不到 root 分区：$root_device" >&2; exit 1; }

if findmnt -rn -S "$boot_device" >/dev/null || findmnt -rn -S "$root_device" >/dev/null; then
	echo "目标分区已挂载，请先卸载 $boot_device 和 $root_device。" >&2
	findmnt -rn -S "$boot_device" 2>/dev/null || true
	findmnt -rn -S "$root_device" 2>/dev/null || true
	exit 1
fi

shopt -s nullglob
bootloaders=("$BOOT_SOURCE"/bootloader*.bin)
shopt -u nullglob
if [[ ${#bootloaders[@]} -ne 1 ]]; then
	echo "boot-partition 中必须有且只能有一个 bootloader*.bin，当前数量：${#bootloaders[@]}" >&2
	exit 1
fi
bootloader_source=${bootloaders[0]}

if [[ -f $SCRIPT_DIR/SHA256SUMS ]]; then
	echo "正在校验 release 文件……"
	(cd "$SCRIPT_DIR" && sha256sum -c SHA256SUMS)
fi

echo
echo "即将部署 Zion release："
echo "  整盘设备：$device"
echo "  boot 分区：$boot_device（覆盖同名文件，保留其他文件）"
echo "  root 分区：$root_device（只删除 /root/zion-tests）"
lsblk -o NAME,SIZE,FSTYPE,LABEL,MOUNTPOINTS,MODEL "$device"
echo
confirmation=
read -r -p "确认目标无误后输入 YES：" confirmation || true
[[ $confirmation == YES ]] || { echo "已取消。"; exit 1; }

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
	echo "boot 分区挂载验证失败" >&2
	exit 1
}
findmnt -rn -M "$root_mount" -S "$root_device" >/dev/null || {
	echo "root 分区挂载验证失败" >&2
	exit 1
}

echo "正在更新 boot 分区……"
shopt -s dotglob nullglob
for source in "$BOOT_SOURCE"/*; do
	[[ $source == "$bootloader_source" ]] && continue
	cp -a -- "$source" "$boot_mount/"
done
shopt -u dotglob nullglob
install -m 0644 "$bootloader_source" "$boot_mount/bootloader.bin"

echo "正在替换 root 分区中的 /root/zion-tests……"
mkdir -p "$root_mount/root"
rm -rf -- "$root_mount/root/zion-tests"
cp -a -- "$ROOT_SOURCE" "$root_mount/root/zion-tests"

sync
umount "$root_mount"
root_mounted=0
umount "$boot_mount"
boot_mounted=0

echo "部署完成："
echo "  $boot_device：boot 文件已覆盖，bootloader 已写为 bootloader.bin"
echo "  $root_device：/root/zion-tests 已完整替换"
echo "现在可以安全移除设备。"
