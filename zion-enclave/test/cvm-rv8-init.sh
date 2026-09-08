#!/bin/sh
# Run the pinned RV8 suite in Zion enclaves owned by a Zion CVM.

BB=/usr/bin/busybox
PATH=/bin:/sbin:/usr/bin:/usr/sbin

$BB mount -t proc proc /proc
$BB mount -t sysfs sysfs /sys
$BB mount -t devtmpfs devtmpfs /dev
$BB dmesg -n 4

finish() {
	$BB sync
	$BB poweroff -f
	$BB sleep 1
	exec sh
}

fail() {
	echo "[CVM-RV8] FAIL: $*"
	finish
}

cd /rv8 || fail "artifact directory missing"
$BB sha256sum -c MANIFEST.sha256 || fail "artifact manifest"

$BB insmod /rv8/zion-driver.ko nested_cvm=1 ||
	fail "nested Zion driver load"
[ -e /dev/zion_enclave ] || fail "Zion device node missing"
$BB dmesg | $BB grep -Fq \
	'zion_enclave: nested CVM mode; using parent-owned SM pool' ||
	fail "driver did not enter nested CVM mode"

passes=0
failures=0
run_case() {
	program="$1"
	expected="$2"
	freemem_kib="$3"
	case_log="/tmp/cvm-rv8-$program.log"

	echo
	echo "[CVM-RV8] RUN: enclave/$program"
	set +e
	/rv8/test-runner "/rv8/bin/$program" /rv8/eyrie-rv8 \
		/rv8/loader-rv8.bin --utm-size 256 \
		--freemem-size "$freemem_kib" --retval 0 \
		>"$case_log" 2>&1
	rc=$?
	set -e
	$BB cat "$case_log"
	output_ok=1
	if [ "$program" = dhrystone ]; then
		$BB grep -Eq '^Dhrystone\(1\.1-mc\), 10000000 passes, [1-9][0-9]* microseconds, [1-9][0-9]* DMIPS$' \
			"$case_log" || output_ok=0
	else
		$BB grep -Fqx "$expected" "$case_log" || output_ok=0
	fi
	if [ "$rc" -eq 0 ] && [ "$output_ok" -eq 1 ]; then
		echo "[CVM-RV8] PASS: enclave/$program"
		passes=$((passes + 1))
	else
		echo "[CVM-RV8] FAIL: enclave/$program (exit=$rc, expected=$expected)"
		failures=$((failures + 1))
	fi
}

run_case qsort 3161985 229376
run_case bigint '23 ^ 111121 has 151317 digits' 49152
run_case dhrystone 'Dhrystone(1.1-mc), 10000000 passes' 49152
run_case miniz Success. 49152
run_case primes 33333331 49152
run_case sha512 \
	ebdd6f20865ff41e3613b633b93c9b89c15d58fd9d64497f5b22554a7fe33757357cfa622f6fb4f40beadc02d18539ecd79e2da126b662839d296c41acbc2 \
	49152
run_case aes 0 114688
run_case norx 0 114688

$BB rmmod zion_driver 2>/dev/null || true
if [ "$passes" -eq 8 ] && [ "$failures" -eq 0 ]; then
	echo "[CVM-RV8] PASS: enclave 8/8"
else
	echo "[CVM-RV8] FAIL: passes=$passes failures=$failures expected=8"
fi
finish
