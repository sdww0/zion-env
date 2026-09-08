#!/bin/sh

mount -t proc proc /proc
mount -t sysfs sysfs /sys
mount -t devtmpfs devtmpfs /dev

fail_and_poweroff() {
	echo "[RV8-QEMU] FAIL: $*"
	sync
	poweroff -f
}

mkdir -p /mnt
mount -t 9p -o trans=virtio,version=9p2000.L hostshare /mnt ||
	fail_and_poweroff "9p mount"

cd /mnt || fail_and_poweroff "shared directory"
sha256sum -c MANIFEST.sha256 || fail_and_poweroff "artifact manifest"

native_enclave_cpu=-1
for arg in $(cat /proc/cmdline); do
	case "$arg" in
		zion_native_enclave_cpu=*) native_enclave_cpu=${arg#*=} ;;
	esac
done
insmod /mnt/zion-driver.ko native_enclave_cpu="$native_enclave_cpu" ||
	fail_and_poweroff "driver load"
[ -e /dev/zion_enclave ] || fail_and_poweroff "device node missing"
grep -Fqx -- "$native_enclave_cpu" \
	/sys/module/zion_driver/parameters/native_enclave_cpu ||
	fail_and_poweroff "requested enclave CPU backend is not active"
dmesg | grep -Fq 'SM pool reserved at 0xf0000000 (256 MB)' ||
	fail_and_poweroff "trusted pool reservation"

failures=0
passes=0
run_case() {
	mode="$1"
	program="$2"
	expected="$3"
	shift 3
	echo ""
	echo "[RV8] RUN: $mode/$program"
	case_log="/tmp/rv8-$mode-$program.log"
	set +e
	"$@" >"$case_log" 2>&1
	rc=$?
	set -e
	cat "$case_log"
	output_ok=1
	if [ "$program" = dhrystone ]; then
		grep -Eq '^Dhrystone\(1\.1-mc\), 10000000 passes, [1-9][0-9]* microseconds, [1-9][0-9]* DMIPS$' \
			"$case_log" || output_ok=0
	else
		grep -Fqx "$expected" "$case_log" || output_ok=0
	fi
	if [ "$rc" -eq 0 ] && [ "$output_ok" -eq 1 ]; then
		echo "[RV8] PASS: $mode/$program"
		passes=$((passes + 1))
	else
		echo "[RV8] FAIL: $mode/$program (exit=$rc, expected=$expected)"
		failures=$((failures + 1))
	fi
}

run_case native aes 0 /mnt/rv8/aes
run_case native bigint '23 ^ 111121 has 151317 digits' /mnt/rv8/bigint
run_case native dhrystone 'Dhrystone(1.1-mc), 10000000 passes' \
	/mnt/rv8/dhrystone
run_case native miniz Success. /mnt/rv8/miniz
run_case native norx 0 /mnt/rv8/norx
run_case native primes 33333331 /mnt/rv8/primes
run_case native qsort 3161985 /mnt/rv8/qsort
run_case native sha512 \
	ebdd6f20865ff41e3613b633b93c9b89c15d58fd9d64497f5b22554a7fe33757357cfa622f6fb4f40beadc02d18539ecd79e2da126b662839d296c41acbc2 \
	/mnt/rv8/sha512

run_enclave_case() {
	program="$1"
	expected="$2"
	freemem_kib="$3"
	cpu="$4"
	run_case "enclave-cpu$cpu" "$program" "$expected" taskset -c "$cpu" \
		/mnt/test-runner \
		"/mnt/rv8/$program" /mnt/eyrie-rv8 /mnt/loader-rv8.bin \
		--utm-size 256 --freemem-size "$freemem_kib" --retval 0
}

run_enclave_case bigint '23 ^ 111121 has 151317 digits' 49152 0
run_enclave_case dhrystone 'Dhrystone(1.1-mc), 10000000 passes' 49152 1
run_enclave_case miniz Success. 49152 2
run_enclave_case primes 33333331 49152 3
run_enclave_case sha512 \
	ebdd6f20865ff41e3613b633b93c9b89c15d58fd9d64497f5b22554a7fe33757357cfa622f6fb4f40beadc02d18539ecd79e2da126b662839d296c41acbc2 \
	49152 0
run_enclave_case aes 0 114688 1
run_enclave_case norx 0 114688 2
run_enclave_case qsort 3161985 229376 3

# The hardware failure being guarded against only appeared after the runner
# migrated to a non-boot hart during a long enclave workload.  Exercise the
# same workload on every hart, not merely one case per hart.
for cpu in 0 1 2 3; do
	run_enclave_case miniz Success. 49152 "$cpu"
done

rmmod zion-driver 2>/dev/null || true
sync
if [ "$failures" -eq 0 ] && [ "$passes" -eq 20 ]; then
	echo "[RV8-QEMU] PASS: native 8/8, enclave 8/8, miniz all-hart 4/4"
else
	echo "[RV8-QEMU] FAIL: passes=$passes failures=$failures expected=20"
fi
poweroff -f
