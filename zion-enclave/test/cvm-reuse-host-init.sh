#!/bin/sh
# Outer REE init: one unified driver serves a native enclave and two CVMs.

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
extent_id=

fail()
{
	echo "[CVM-REUSE-QEMU] FAIL: $*"
	if [ -n "$nested_pid" ]; then
		$BB kill "$nested_pid" 2>/dev/null || true
		wait "$nested_pid" 2>/dev/null || true
	fi
	$BB sync
	$BB poweroff -f
	$BB sleep 1
	exec sh
}

destroy_failure_count()
{
	$BB dmesg | $BB grep -Ec 'KVM: CVM.*(destroy|cleanup) failed' || true
}

require_no_zion_clients()
{
	for fd in /proc/[0-9]*/fd/*; do
		[ -e "$fd" ] || continue
		target=$($BB readlink "$fd" 2>/dev/null || true)
		case "$target" in
			/dev/kvm|/dev/zion_cvm|/dev/zion_enclave)
				pid=${fd#/proc/}
				pid=${pid%%/*}
				fail "active Zion client pid=$pid holds $target"
				;;
		esac
	done
}

verify_idle_extent()
{
	label=$1
	id=$2
	status=$(/cvm/tvm-control query "$id" 2>&1) ||
		fail "cannot query $label extent $id: $status"
	echo "[CVM-REUSE-QEMU] $label $status"
	state=$(echo "$status" | $BB sed -n \
		's/.* state=\([0-9][0-9]*\) .*/\1/p')
	free=$(echo "$status" | $BB sed -n \
		's/.* free=\([0-9][0-9]*\)\/[0-9][0-9]* .*/\1/p')
	total=$(echo "$status" | $BB sed -n \
		's/.* free=[0-9][0-9]*\/\([0-9][0-9]*\) .*/\1/p')
	[ "$state" = 2 ] || fail "$label extent is not online (state=${state:-?})"
	[ -n "$total" ] && [ "$total" -gt 0 ] ||
		fail "$label extent block count is invalid"
	[ "$free" = "$total" ] ||
		fail "$label extent leaked blocks (free=${free:-?}/${total:-?})"
}

observe_extent_use()
{
	id=$1
	status=$(/cvm/tvm-control query "$id" 2>/dev/null) || return
	free=$(echo "$status" | $BB sed -n \
		's/.* free=\([0-9][0-9]*\)\/[0-9][0-9]* .*/\1/p')
	total=$(echo "$status" | $BB sed -n \
		's/.* free=[0-9][0-9]*\/\([0-9][0-9]*\) .*/\1/p')
	[ -n "$free" ] && [ -n "$total" ] && [ "$free" -lt "$total" ]
}

run_nested_rv8()
{
	iteration=$1
	guest_log="/tmp/cvm-reuse-$iteration.log"
	: >"$guest_log"
	failures_before=$(destroy_failure_count)
	used=0

	echo "[CVM-REUSE-QEMU] starting CVM iteration $iteration"
	/cvm/qemu-system-riscv64 \
		-m 512M -smp 1 --enable-kvm \
		-display none -monitor none -serial stdio -machine virt \
		-kernel /cvm/guest_kernel_image \
		-initrd /cvm/guest_rv8_initrd.img \
		-append 'console=ttyS0 loglevel=6 memmap=2M$0x80000000 cma=256M rdinit=/init' \
		>"$guest_log" 2>&1 &
	nested_pid=$!
	attempt=0
	while $BB kill -0 "$nested_pid" 2>/dev/null; do
		if $BB grep -Fq ' pc       ' "$guest_log"; then
			$BB cat "$guest_log"
			fail "iteration $iteration entered KVM internal-error state"
		fi
		if observe_extent_use 0 || observe_extent_use "$extent_id"; then
			used=1
		fi
		attempt=$((attempt + 1))
		[ "$attempt" -lt 5400 ] || fail "iteration $iteration timed out"
		$BB sleep 1
	done

	set +e
	wait "$nested_pid"
	rc=$?
	set -e
	nested_pid=
	$BB cat "$guest_log"
	[ "$rc" -eq 0 ] || fail "iteration $iteration QEMU exited with status $rc"
	$BB grep -Fq '[CVM-RV8] PASS: enclave 8/8' "$guest_log" ||
		fail "iteration $iteration suite marker is absent"
	[ "$($BB grep -Fc '[CVM-RV8] PASS: enclave/' "$guest_log")" -eq 8 ] ||
		fail "iteration $iteration enclave PASS count is not 8"
	if $BB grep -Eq '\[CVM-RV8\] FAIL:|Kernel panic|Oops:' "$guest_log"; then
		fail "iteration $iteration reported failure or kernel crash"
	fi
	[ "$used" -eq 1 ] || fail "iteration $iteration did not consume trusted memory"
	require_no_zion_clients
	[ "$(destroy_failure_count)" -eq "$failures_before" ] ||
		fail "iteration $iteration CVM destroy/cleanup failed"
	verify_idle_extent core 0
	verify_idle_extent dynamic "$extent_id"
	echo "[CVM-REUSE-QEMU] PASS: iteration $iteration destroyed; pool fully idle"
}

echo "[CVM-REUSE-QEMU] loading unified Zion driver once"
$BB insmod /cvm/zion-driver.ko native_enclave_cpu=-1 ||
	fail "unified Zion driver load"
$BB grep -Fqx -- -1 /sys/module/zion_driver/parameters/native_enclave_cpu ||
	fail "native enclave execution is unexpectedly CPU-pinned"
[ -c /dev/zion_cvm ] || fail "/dev/zion_cvm is absent"
[ -c /dev/zion_enclave ] || fail "/dev/zion_enclave is absent"
verify_idle_extent core 0

echo "[CVM-REUSE-QEMU] starting native enclave on the unified pool"
native_log=/tmp/unified-native-enclave.log
set +e
/cvm/enclave/hello-runner /cvm/enclave/hello /cvm/enclave/eyrie-rt \
	/cvm/enclave/loader.bin >"$native_log" 2>&1
native_rc=$?
set -e
$BB cat "$native_log"
[ "$native_rc" -eq 0 ] || fail "native enclave exited with status $native_rc"
$BB grep -Fq '[ZION] PASS: hello enclave lifecycle' "$native_log" ||
	fail "native enclave lifecycle marker is absent"
require_no_zion_clients
verify_idle_extent core 0
echo "[CVM-REUSE-QEMU] PASS: native enclave destroyed; unified pool fully idle"

extent_output=$(/cvm/tvm-control extend 32768) ||
	fail "128 MiB trusted extent allocation"
echo "[CVM-REUSE-QEMU] $extent_output"
extent_id=$(echo "$extent_output" | $BB sed -n \
	's/^extent_id=\([0-9][0-9]*\)$/\1/p')
case "$extent_id" in
	''|*[!0-9]*) fail "invalid dynamic extent id" ;;
esac

verify_idle_extent core 0
verify_idle_extent dynamic "$extent_id"
run_nested_rv8 1

echo "[CVM-REUSE-QEMU] validating loaded-driver reuse gate"
$BB grep -Eq '^zion_driver ' /proc/modules || fail "unified driver was not retained"
[ -c /dev/zion_cvm ] || fail "/dev/zion_cvm disappeared"
[ -c /dev/zion_enclave ] || fail "/dev/zion_enclave disappeared"
require_no_zion_clients
[ "$(destroy_failure_count)" -eq 0 ] || fail "prior CVM destroy failure is present"
verify_idle_extent core 0
verify_idle_extent dynamic "$extent_id"
echo "[CVM-REUSE-QEMU] reusing the unified Zion driver and reserved core"

run_nested_rv8 2
/cvm/tvm-control shrink "$extent_id" || fail "dynamic extent removal"
echo "[CVM-REUSE-QEMU] PASS: one unified driver served native enclave plus two CVM/enclave/RV8 runs"
$BB sync
$BB poweroff -f
$BB sleep 1
exec sh
