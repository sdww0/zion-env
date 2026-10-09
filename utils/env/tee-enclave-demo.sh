#!/bin/sh
set -eu

cd "$(dirname "$0")"
QEMU="${QEMU:-/usr/local/qemu/bin/qemu-system-riscv64}"
KERNEL="${KERNEL:-./guest_kernel_image}"
INITRD="${INITRD:-./initrd-enclave.img}"
SSH_PORT="${SSH_PORT:-10022}"
LOG="${LOG:-./cvm-demo.log}"
PIDFILE="${PIDFILE:-./cvm-demo.pid}"

for file in "$QEMU" "$KERNEL" "$INITRD"; do
    [ -f "$file" ] || { echo "[ZION DEMO] FAIL: missing $file" >&2; exit 1; }
done
[ -e /dev/kvm ] && [ -e /dev/tvm ] || {
    echo '[ZION DEMO] Initialize Host driver and trusted pool first' >&2
    exit 1
}
if [ -f "$PIDFILE" ]; then
    read -r pid < "$PIDFILE"
    if kill -0 "$pid" 2>/dev/null; then
        echo "[ZION DEMO] FAIL: PID $pid is still running" >&2
        exit 1
    fi
fi

"$QEMU" -machine virt -cpu rv64 -m 512M -smp 1 \
    -enable-kvm -zion-cvm -kernel "$KERNEL" -initrd "$INITRD" \
    -netdev "user,id=net0,hostfwd=tcp:127.0.0.1:$SSH_PORT-:22" \
    -device virtio-net-device,netdev=net0 \
    -display none -monitor none -serial "file:$LOG" \
    -daemonize -pidfile "$PIDFILE" \
    -append 'console=ttyS0 loglevel=7 memmap=2M$0x80000000 rdinit=/etc/zion-ssh.sh init=/etc/zion-ssh.sh'

echo "[ZION DEMO] QEMU started; Guest boot is not yet confirmed. Log: $LOG"
echo "[ZION DEMO] From Host: ssh -p $SSH_PORT root@127.0.0.1 (root/debian)"
echo '[ZION DEMO] Inside Guest: /usr/bin/run-zion-enclave-demo'
