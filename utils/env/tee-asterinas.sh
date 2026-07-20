#! /bin/bash

set -e

QEMU_BIN="${QEMU_BIN:-/usr/local/qemu/bin/qemu-system-riscv64}"
ASTER_KERNEL="${ASTER_KERNEL:-asterinas_kernel}"
ASTER_INITRAMFS="${ASTER_INITRAMFS:-initrd-ssh.img}"
ASTER_MEM="${ASTER_MEM:-512M}"
ASTER_SMP="${ASTER_SMP:-1}"
ASTER_LOG_LEVEL="${ASTER_LOG_LEVEL:-error}"
ASTER_CONSOLE="${ASTER_CONSOLE:-ttyS0}"
ASTER_QEMU_LOG="${ASTER_QEMU_LOG:-asterinas-qemu.log}"
ASTER_DAEMONIZE="${ASTER_DAEMONIZE:-0}"
ASTER_NET_DUMP="${ASTER_NET_DUMP:-}"
SSH_LISTEN="${SSH_LISTEN:-0.0.0.0}"
SSH_PORT="${SSH_PORT:-10022}"

for file in "$ASTER_KERNEL" "$ASTER_INITRAMFS"; do
	if [ ! -f "$file" ]; then
		echo "missing Asterinas artifact: $file" >&2
		exit 1
	fi
done

if [ "$ASTER_CONSOLE" != "ttyS0" ]; then
	echo "Only ttyS0 is supported for Zion Asterinas boot" >&2
	exit 1
fi

qemu_args=(
	-m "$ASTER_MEM"
	--enable-kvm
	-zion-cvm
	-cpu rv64,svpbmt=true
	-machine virt
	-smp "$ASTER_SMP"
	--no-reboot
	-display none
	-kernel "$ASTER_KERNEL"
	-initrd "$ASTER_INITRAMFS"
	-netdev user,id=net0,hostfwd=tcp:"$SSH_LISTEN":"$SSH_PORT"-:22
	-device virtio-net-device,netdev=net0,csum=off,guest_csum=off,gso=off,guest_tso4=off,guest_tso6=off,guest_ecn=off,guest_ufo=off,guest_uso4=off,guest_uso6=off,host_tso4=off,host_tso6=off,host_ecn=off,host_ufo=off,host_uso=off,mrg_rxbuf=off,ctrl_rx=off,ctrl_rx_extra=off,ctrl_vlan=off,ctrl_vq=off,ctrl_guest_offloads=off,ctrl_mac_addr=off,event_idx=off,queue_reset=off,guest_announce=off,indirect_desc=off,packed=off
	-append "SHELL=/bin/sh LOGNAME=root HOME=/ USER=root PATH=/bin:/usr/bin:/sbin:/usr/sbin:/benchmark init=/etc/zion-ssh.sh ostd.log_level=$ASTER_LOG_LEVEL console=$ASTER_CONSOLE"
)

if [ -n "$ASTER_NET_DUMP" ]; then
	qemu_args+=(
		-object "filter-dump,id=netdump,netdev=net0,file=$ASTER_NET_DUMP"
	)
fi

if [ "$ASTER_DAEMONIZE" = "1" ]; then
	qemu_args+=(
		-daemonize
		-serial "file:$ASTER_QEMU_LOG"
		-monitor none
	)
else
	qemu_args+=(
		-nographic
		-serial chardev:mux
		-monitor chardev:mux
		-chardev stdio,id=mux,mux=on,signal=off,logfile="$ASTER_QEMU_LOG"
	)
fi

"$QEMU_BIN" "${qemu_args[@]}"
