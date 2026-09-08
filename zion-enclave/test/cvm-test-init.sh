#!/bin/sh

PATH=/bin:/sbin:/usr/bin:/usr/sbin

expected_digest=1517525ef1ba7710a873d7fed6246ca2fe83b9a48a39a595f88e299b332f1af0
cpu_count=$(nproc)
smp_rc=0
lifecycle_rc=0
attest_challenge=${cvm_attest_challenge:-}
fresh_attest_challenge=${cvm_attest_fresh_challenge:-}
cvm_instance=${cvm_instance:-unknown}
cvm_test_mode=${cvm_test_mode:-full}
cvm_smoke_hold_seconds=${cvm_smoke_hold_seconds:-60}

if [ -r /proc/cmdline ]; then
  for argument in $(cat /proc/cmdline); do
    case "$argument" in
      cvm_attest_challenge=*)
        attest_challenge=${argument#cvm_attest_challenge=}
        ;;
      cvm_attest_fresh_challenge=*)
        fresh_attest_challenge=${argument#cvm_attest_fresh_challenge=}
        ;;
      cvm_instance=*)
        cvm_instance=${argument#cvm_instance=}
        ;;
      cvm_test_mode=*)
        cvm_test_mode=${argument#cvm_test_mode=}
        ;;
      cvm_smoke_hold_seconds=*)
        cvm_smoke_hold_seconds=${argument#cvm_smoke_hold_seconds=}
        ;;
    esac
  done
fi

case "$cvm_instance" in
  primary|secondary|tertiary|quaternary) ;;
  *)
    echo "[CVM INSTANCE] FAIL: invalid instance label '$cvm_instance'"
    lifecycle_rc=1
    ;;
esac
echo "[CVM INSTANCE] START: $cvm_instance"

case "$cvm_test_mode" in
  full) ;;
  smoke)
    case "$cvm_smoke_hold_seconds" in
      ''|*[!0-9]*)
        echo "[CVM TEST] FAIL: invalid smoke hold '$cvm_smoke_hold_seconds'"
        exec sh
        ;;
    esac
    echo "[CVM SMOKE] PASS: booted instance $cvm_instance"
    echo "[CVM INSTANCE] PASS: $cvm_instance"
    sleep "$cvm_smoke_hold_seconds"
    sync
    poweroff -f
    echo "[CVM TEST] poweroff failed; entering emergency shell"
    exec sh
    ;;
  *)
    echo "[CVM TEST] FAIL: invalid test mode '$cvm_test_mode'"
    lifecycle_rc=1
    ;;
esac

if [ -z "$attest_challenge" ] || [ -z "$fresh_attest_challenge" ]; then
  echo "[CVM ENCLAVE] FAIL: verifier challenges missing from kernel command line"
  lifecycle_rc=1
elif insmod /cvm-enclave-lifecycle.ko \
    challenge="$attest_challenge" fresh_challenge="$fresh_attest_challenge"; then
  echo "[CVM ENCLAVE] PASS: lifecycle module loaded"
  rmmod cvm_enclave_lifecycle || lifecycle_rc=1
else
  echo "[CVM ENCLAVE] FAIL: nested create/destroy regression failed"
  lifecycle_rc=1
fi

echo "[CVM SMP] online CPUs: $cpu_count"
if [ "$cpu_count" -lt 2 ]; then
  echo "[CVM SMP] FAIL: expected at least 2 online CPUs"
  smp_rc=1
elif ! taskset -c 0 sh -c 'exit 0' || ! taskset -c 1 sh -c 'exit 0'; then
  echo "[CVM SMP] FAIL: could not bind work to CPU0 and CPU1"
  smp_rc=1
else
  iteration=0
  while [ "$iteration" -lt 8 ]; do
    digest=$(taskset -c 0 yes z | taskset -c 1 head -c 1048576 |
      taskset -c 1 sha256sum)
    if [ "${digest%% *}" != "$expected_digest" ]; then
      smp_rc=1
      break
    fi

    digest=$(taskset -c 1 yes z | taskset -c 0 head -c 1048576 |
      taskset -c 0 sha256sum)
    if [ "${digest%% *}" != "$expected_digest" ]; then
      smp_rc=1
      break
    fi
    iteration=$((iteration + 1))
  done
fi

if [ "$smp_rc" -eq 0 ] && [ "$lifecycle_rc" -eq 0 ]; then
  echo "[CVM SMP] PASS: cross-CPU wakeup workload completed"
  /etc/sqlite.sh
  rc=$?
else
  echo "[CVM TEST] FAIL: enclave lifecycle or cross-CPU workload failed"
  rc=1
fi

echo
if [ "$rc" -eq 0 ]; then
  echo "[CVM TEST] PASS: SQLite workload completed"
  echo "[CVM INSTANCE] PASS: $cvm_instance"
else
  echo "[CVM TEST] FAIL: SQLite workload exited with status $rc"
  echo "[CVM INSTANCE] FAIL: $cvm_instance"
fi

sync
poweroff -f

echo "[CVM TEST] poweroff failed; entering emergency shell"
exec sh
