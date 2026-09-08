#!/bin/sh
# Outer REE init: create one CVM and validate its complete enclave/RV8 log.

BB=/bin/busybox
PATH=/bin:/sbin:/usr/bin:/usr/sbin:/cvm
LD_LIBRARY_PATH=/lib/riscv64-linux-gnu:/usr/lib/riscv64-linux-gnu
export PATH LD_LIBRARY_PATH

$BB mount -t proc proc /proc
$BB mount -t sysfs sysfs /sys
$BB mount -t devtmpfs devtmpfs /dev
$BB dmesg -n 4
$BB mkdir -p /tmp

nested_pid=
fail() {
	echo "[CVM-RV8-QEMU] FAIL: $*"
	if [ -n "$nested_pid" ]; then
		$BB kill "$nested_pid" 2>/dev/null || true
		wait "$nested_pid" 2>/dev/null || true
	fi
	$BB sync
	$BB poweroff -f
	$BB sleep 1
	exec sh
}

echo "[CVM-RV8-QEMU] Loading TVM driver"
$BB insmod /cvm/tvm-driver.ko || fail "TVM driver load"

echo "[CVM-RV8-QEMU] Reserving 512 MiB trusted core"
/cvm/tvm-control tvm 131072 || fail "trusted core reservation"

echo "[CVM-RV8-QEMU] Adding 128 MiB trusted extent"
extent_output=$(/cvm/tvm-control extend 32768) ||
	fail "trusted extent allocation"
echo "$extent_output"
extent_id=$(echo "$extent_output" | $BB sed -n \
	's/^extent_id=\([0-9][0-9]*\)$/\1/p')
case "$extent_id" in
	''|*[!0-9]*) fail "invalid trusted extent id" ;;
esac
/cvm/tvm-control query "$extent_id" || fail "trusted extent query"

echo "[CVM-RV8-QEMU] Starting 512 MiB CVM for nested enclave/RV8"
guest_log=/tmp/cvm-rv8-guest.log
: > "$guest_log"
/cvm/qemu-system-riscv64 \
	-m 512M -smp 1 --enable-kvm \
	-display none -monitor none -serial stdio \
	-machine virt \
	-kernel /cvm/guest_kernel_image \
	-initrd /cvm/guest_rv8_initrd.img \
	-append 'console=ttyS0 loglevel=6 memmap=2M$0x80000000 cma=256M rdinit=/init' \
	>"$guest_log" 2>&1 &
nested_pid=$!

extent_used=0
attempt=0
while $BB kill -0 "$nested_pid" 2>/dev/null; do
	if $BB grep -Fq ' pc       ' "$guest_log"; then
		$BB cat "$guest_log"
		fail "nested KVM entered internal-error stop state"
	fi
	query_output=$(/cvm/tvm-control query "$extent_id" 2>/dev/null) ||
		fail "trusted extent disappeared during nested run"
	free_blocks=$(echo "$query_output" | $BB sed -n \
		's/.* free=\([0-9][0-9]*\)\/\([0-9][0-9]*\) .*/\1/p')
	total_blocks=$(echo "$query_output" | $BB sed -n \
		's/.* free=\([0-9][0-9]*\)\/\([0-9][0-9]*\) .*/\2/p')
	if [ -n "$free_blocks" ] && [ -n "$total_blocks" ] &&
	   [ "$free_blocks" -lt "$total_blocks" ]; then
		extent_used=1
	fi
	attempt=$((attempt + 1))
	[ "$attempt" -lt 5400 ] || fail "nested RV8 timeout"
	$BB sleep 1
done

set +e
wait "$nested_pid"
nested_rc=$?
set -e
nested_pid=

echo "[CVM-RV8-QEMU] Nested guest log follows"
$BB cat "$guest_log"
[ "$nested_rc" -eq 0 ] || fail "nested QEMU exited with status $nested_rc"
$BB grep -Fq '[CVM-RV8] PASS: enclave 8/8' "$guest_log" ||
	fail "complete nested PASS marker missing"
[ "$($BB grep -Fc '[CVM-RV8] PASS: enclave/' "$guest_log")" -eq 8 ] ||
	fail "nested enclave PASS count is not 8"
if $BB grep -Eqi '\[CVM-RV8\] FAIL:|Kernel panic|Oops:|rcu.*stall|soft lockup' "$guest_log"; then
	fail "nested guest reported failure, lockup, or RCU stall"
fi
[ "$extent_used" -eq 1 ] ||
	fail "nested workload did not consume the added trusted extent"

/cvm/tvm-control shrink "$extent_id" ||
	fail "trusted extent was not reclaimed after CVM exit"
echo "[CVM-RV8-QEMU] PASS: Host -> CVM -> enclave -> RV8 8/8"
$BB sync
$BB poweroff -f
$BB sleep 1
exec sh
