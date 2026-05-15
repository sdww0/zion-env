#! /bin/bash

set -e

SSH_PORT="${SSH_PORT:-10022}"
SSH_LISTEN="${SSH_LISTEN:-0.0.0.0}"
BLK_IMG="${BLK_IMG:-cvm-virtio-blk.img}"

if [ ! -f "$BLK_IMG" ]; then
	truncate -s 64M "$BLK_IMG"
fi

/usr/local/qemu/bin/qemu-system-riscv64 -m 512M \
	--enable-kvm \
	-cpu rv64 \
	-nographic \
	-machine virt \
	-kernel ./guest_kernel_image \
	-initrd initrd-ssh.img \
	-drive file="$BLK_IMG",if=none,format=raw,id=vdisk \
	-device virtio-blk-device,drive=vdisk \
	-netdev user,id=net0,hostfwd=tcp:"$SSH_LISTEN":"$SSH_PORT"-:22 \
	-device virtio-net-device,netdev=net0 \
	-append "console=ttyS0 ostd.log_level=error loglevel=7 memmap=2M\$0x80000000 rdinit=/etc/zion-ssh.sh init=/etc/zion-ssh.sh"
