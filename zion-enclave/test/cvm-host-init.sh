#!/bin/sh

mount -t proc proc /proc
mount -t sysfs sysfs /sys
mount -t devtmpfs devtmpfs /dev

# Suppress verbose debug printk calls from the recovered host KVM tree while
# keeping errors visible on the serial console.
dmesg -n 3

export PATH=/bin:/sbin:/usr/bin:/usr/sbin:/cvm
export LD_LIBRARY_PATH=/lib/riscv64-linux-gnu:/usr/lib/riscv64-linux-gnu

CVM_GUEST_COUNT=2
CVM_GUEST_LABELS="primary secondary"
CVM_GUEST_MEMORY=512M
CVM_EXTENT_PAGES=32768
CVM_EXTENT_MIB=128
CVM_GUEST_CPUS=2
CVM_TEST_MODE=full
CVM_SMOKE_HOLD_SECONDS=60
qemu_pids=
qemu_pid_table=/tmp/cvm-qemu-pids
qemu_rc_table=/tmp/cvm-qemu-rcs

guest_log_path() {
  printf '/tmp/cvm-guest-%s.log\n' "$1"
}

lookup_instance_value() {
  table=$1
  target=$2
  [ -f "$table" ] || return 1
  while read -r instance value rest; do
    [ "$instance" = "$target" ] || continue
    printf '%s\n' "$value"
    return 0
  done < "$table"
  return 1
}

remember_instance_value() {
  table=$1
  instance=$2
  value=$3
  printf '%s %s\n' "$instance" "$value" >> "$table"
}

guest_pid_for() {
  lookup_instance_value "$qemu_pid_table" "$1"
}

guest_rc_for() {
  lookup_instance_value "$qemu_rc_table" "$1"
}

dump_nested_guest_logs() {
  for instance in $CVM_GUEST_LABELS; do
    guest_log=$(guest_log_path "$instance")
    [ -f "$guest_log" ] || continue
    echo "[CVM MULTI] $instance guest log follows"
    cat "$guest_log"
  done
}

cleanup_nested_guests() {
  for pid in $qemu_pids; do
    case "$pid" in
      ''|*[!0-9]*) continue ;;
    esac
    kill -CONT "$pid" 2>/dev/null
    kill "$pid" 2>/dev/null
  done
  for pid in $qemu_pids; do
    case "$pid" in
      ''|*[!0-9]*) continue ;;
    esac
    wait "$pid" 2>/dev/null
  done
  qemu_pids=
}

echo
echo "========================================="
echo "  Zion CVM host recovery environment"
echo "========================================="
echo

fail_to_shell() {
  echo "[FAIL] $1"
  cleanup_nested_guests
  if grep -qw cvm-auto /proc/cmdline; then
    echo "[CVM] automatic test failed; powering off host"
    sync
    poweroff -f
  fi
  exec setsid sh -c 'exec sh </dev/ttyS0 >/dev/ttyS0 2>&1'
}

for argument in $(cat /proc/cmdline); do
  case "$argument" in
    cvm_guest_count=*)
      CVM_GUEST_COUNT=${argument#cvm_guest_count=}
      ;;
    cvm_test_mode=*)
      CVM_TEST_MODE=${argument#cvm_test_mode=}
      ;;
    cvm_smoke_hold_seconds=*)
      CVM_SMOKE_HOLD_SECONDS=${argument#cvm_smoke_hold_seconds=}
      ;;
  esac
done
case "$CVM_TEST_MODE" in
  full|smoke) ;;
  *) fail_to_shell "unsupported cvm_test_mode=$CVM_TEST_MODE" ;;
esac
case "$CVM_SMOKE_HOLD_SECONDS" in
  ''|*[!0-9]*) fail_to_shell "unsupported cvm_smoke_hold_seconds=$CVM_SMOKE_HOLD_SECONDS" ;;
esac
case "$CVM_GUEST_COUNT" in
  2)
    CVM_GUEST_LABELS="primary secondary"
    CVM_GUEST_MEMORY=512M
    CVM_EXTENT_PAGES=32768
    CVM_EXTENT_MIB=128
    CVM_GUEST_CPUS=2
    ;;
  4)
    CVM_GUEST_LABELS="primary secondary tertiary quaternary"
    CVM_GUEST_MEMORY=256M
    CVM_EXTENT_PAGES=98304
    CVM_EXTENT_MIB=384
    CVM_GUEST_CPUS=2
    ;;
  *)
    fail_to_shell "unsupported cvm_guest_count=$CVM_GUEST_COUNT"
    ;;
esac
if [ "$CVM_TEST_MODE" = smoke ]; then
  CVM_GUEST_CPUS=1
fi

echo "[1/8] Loading tvm-driver.ko"
if ! insmod /cvm/tvm-driver.ko; then
  fail_to_shell "could not load tvm-driver.ko"
fi
if [ "$(stat -c %a /dev/zion_cvm)" != 600 ]; then
  fail_to_shell "/dev/zion_cvm is not restricted to mode 0600"
fi
if /cvm/tvm-control phys_memory_access 0 >/dev/null 2>&1; then
  fail_to_shell "unsafe TVM physical-memory ioctl is enabled"
fi
echo "[CVM DRIVER] PASS: root-only device and unsafe debug ioctls disabled"

echo "[2/8] Reserving 512 MiB trusted core"
if ! /cvm/tvm-control tvm 131072; then
  fail_to_shell "CVM memory reservation failed"
fi

echo "[3/8] Extending trusted data pool by ${CVM_EXTENT_MIB} MiB"
if ! extent_output=$(/cvm/tvm-control extend "$CVM_EXTENT_PAGES"); then
  fail_to_shell "trusted pool extension failed"
fi
echo "$extent_output"
extent_id=$(echo "$extent_output" | sed -n 's/^extent_id=\([0-9][0-9]*\)$/\1/p')
case "$extent_id" in
  ''|*[!0-9]*) fail_to_shell "invalid extent id returned by driver" ;;
esac
if ! /cvm/tvm-control query "$extent_id"; then
  fail_to_shell "trusted extent query failed"
fi

echo "[4/8] Exercising direct CVM management SBI validation"
if ! insmod /cvm/cvm-management-negative.ko; then
  fail_to_shell "could not load CVM management regression module"
fi
if ! /cvm/cvm-management-control; then
  fail_to_shell "CVM management SBI validation failed"
fi
if ! /cvm/cvm-management-control capacity 16; then
  fail_to_shell "CVM management capacity validation failed"
fi
if ! rmmod cvm_management_negative; then
  fail_to_shell "could not unload CVM management regression module"
fi

echo "[5/8] Checking native QEMU"
if ! /cvm/qemu-system-riscv64 --version; then
  fail_to_shell "native QEMU or one of its shared libraries is unavailable"
fi

echo "[6/8] Starting ${CVM_GUEST_COUNT} ${CVM_GUEST_MEMORY} CVM guests in ${CVM_TEST_MODE} mode with the added extent online"
guest_append_base='console=ttyS0 loglevel=7 memmap=2M$0x80000000 rdinit=/etc/cvm-test-init.sh'
guest_initrd=/cvm/guest_initrd.img
if [ "$CVM_TEST_MODE" = smoke ]; then
  guest_initrd=/cvm/guest_smoke_initrd.img
fi
[ -f "$guest_initrd" ] || fail_to_shell "missing guest initrd: $guest_initrd"
mkdir -p /tmp
: > "$qemu_pid_table"
: > "$qemu_rc_table"
challenge_values=
for instance in $CVM_GUEST_LABELS; do
  challenge_hex=$(tr -d '-' < /proc/sys/kernel/random/uuid)
  if [ "${#challenge_hex}" -ne 32 ]; then
    fail_to_shell "could not generate verifier challenges"
  fi
  attest=${challenge_hex%????????????????}
  fresh=${challenge_hex#????????????????}
  if [ "$attest" = 0000000000000000 ] ||
     [ "$fresh" = 0000000000000000 ] ||
     [ "$attest" = "$fresh" ]; then
    fail_to_shell "generated verifier challenges are invalid"
  fi
  for existing in $challenge_values; do
    if [ "$attest" = "$existing" ] || [ "$fresh" = "$existing" ]; then
      fail_to_shell "generated verifier challenges are repeated"
    fi
  done
  challenge_values="$challenge_values $attest $fresh"
  guest_log=$(guest_log_path "$instance")
  guest_append="$guest_append_base cvm_instance=$instance"
  guest_append="$guest_append cvm_test_mode=$CVM_TEST_MODE"
  guest_append="$guest_append cvm_smoke_hold_seconds=$CVM_SMOKE_HOLD_SECONDS"
  guest_append="$guest_append cvm_attest_challenge=0x$attest"
  guest_append="$guest_append cvm_attest_fresh_challenge=0x$fresh"
  echo "[CVM] $instance verifier challenges: $attest -> $fresh"
  : > "$guest_log"
  /cvm/qemu-system-riscv64 \
    -m "$CVM_GUEST_MEMORY" \
    -smp "$CVM_GUEST_CPUS" \
    --enable-kvm \
    -display none \
    -monitor none \
    -serial "file:$guest_log" \
    -machine virt \
    -kernel /cvm/guest_kernel_image \
    -initrd "$guest_initrd" \
    -append "$guest_append" &
  pid=$!
  qemu_pids="$qemu_pids $pid"
  remember_instance_value "$qemu_pid_table" "$instance" "$pid"
done

extent_busy=0
concurrent_ready=0
attempt=0
while [ "$attempt" -lt 300 ]; do
  if ! query_output=$(/cvm/tvm-control query "$extent_id"); then
    fail_to_shell "trusted extent disappeared while CVMs were running"
  fi
  free_blocks=$(echo "$query_output" |
    sed -n 's/.* free=\([0-9][0-9]*\)\/\([0-9][0-9]*\) .*/\1/p')
  total_blocks=$(echo "$query_output" |
    sed -n 's/.* free=\([0-9][0-9]*\)\/\([0-9][0-9]*\) .*/\2/p')
  if [ -n "$free_blocks" ] && [ -n "$total_blocks" ] &&
     [ "$free_blocks" -lt "$total_blocks" ]; then
    extent_busy=1
  fi
  all_ready=1
  for instance in $CVM_GUEST_LABELS; do
    guest_log=$(guest_log_path "$instance")
    pid=$(guest_pid_for "$instance") ||
      fail_to_shell "missing nested QEMU pid for $instance"
    if [ "$CVM_TEST_MODE" = smoke ]; then
      ready_marker="[CVM SMOKE] PASS: booted instance $instance"
    else
      ready_marker='[CVM ENCLAVE] PASS: lifecycle module loaded'
    fi
    if ! grep -Fq "$ready_marker" "$guest_log" ||
       ! kill -0 "$pid" 2>/dev/null; then
      all_ready=0
      break
    fi
  done
  if [ "$all_ready" -eq 1 ]; then
    concurrent_ready=1
  fi
  if [ "$concurrent_ready" -eq 1 ] &&
     { [ "$CVM_TEST_MODE" = smoke ] || [ "$extent_busy" -eq 1 ]; }; then
    break
  fi
  for pid in $qemu_pids; do
    kill -0 "$pid" 2>/dev/null || break 2
  done
  sleep 1
  attempt=$((attempt + 1))
done

if [ "$concurrent_ready" -ne 1 ]; then
  dump_nested_guest_logs
  fail_to_shell "$CVM_GUEST_COUNT CVMs did not reach guest execution concurrently"
fi
if [ "$CVM_TEST_MODE" = smoke ]; then
  echo "[CVM MULTI] PASS: $CVM_GUEST_COUNT CVM smoke guests booted concurrently"
else
  echo "[CVM MULTI] PASS: $CVM_GUEST_COUNT CVMs reached nested enclave execution concurrently"
fi
if [ "$CVM_TEST_MODE" != smoke ] && [ "$extent_busy" -ne 1 ]; then
  fail_to_shell "CVMs did not allocate from the added trusted extent"
fi

if [ "$CVM_TEST_MODE" = smoke ]; then
  echo "[CVM SMOKE] Keeping $CVM_GUEST_COUNT guests live for slot accounting"
else
  echo "[CVM] Pausing $CVM_GUEST_COUNT guests to test contraction of an owned extent"
  for pid in $qemu_pids; do
    if ! kill -STOP "$pid" 2>/dev/null; then
      fail_to_shell "could not pause nested QEMUs"
    fi
  done
  for pid in $qemu_pids; do
    attempt=0
    qemu_state=
    while [ "$attempt" -lt 10 ]; do
      qemu_state=$(sed -n 's/^State:[[:space:]]*\([A-Z]\).*/\1/p' \
        "/proc/$pid/status" 2>/dev/null)
      [ "$qemu_state" = T ] && break
      sleep 1
      attempt=$((attempt + 1))
    done
    if [ "$qemu_state" != T ]; then
      fail_to_shell "nested QEMU did not stop for extent contraction test"
    fi
  done
fi

if ! insmod /cvm/cvm-management-negative.ko; then
  fail_to_shell "could not reload CVM management regression module"
fi
remaining_cvm_slots=$((16 - CVM_GUEST_COUNT))
if ! /cvm/cvm-management-control capacity "$remaining_cvm_slots"; then
  fail_to_shell "remaining CVM slot capacity validation failed"
fi
if ! rmmod cvm_management_negative; then
  fail_to_shell "could not unload CVM management regression module"
fi
echo "[CVM MULTI] PASS: 16 CVM slots enforced while $CVM_GUEST_COUNT guests remained live"

if [ "$CVM_TEST_MODE" = smoke ]; then
  echo "[CVM SMOKE] PASS: live guest slot accounting checked without extent pressure"
else
  busy_output=$(/cvm/tvm-control shrink "$extent_id" 2>&1)
  busy_rc=$?
  if [ "$busy_rc" -eq 0 ]; then
    fail_to_shell "in-use trusted extent was removed"
  fi
  case "$busy_output" in
    *"Device or resource busy"*) ;;
    *)
      echo "$busy_output"
      fail_to_shell "in-use trusted extent returned an unexpected error"
      ;;
  esac
  if ! draining_output=$(/cvm/tvm-control query "$extent_id"); then
    fail_to_shell "draining trusted extent is not queryable"
  fi
  echo "$draining_output"
  case "$draining_output" in
    *" state=3 "*)
      echo "[CVM] PASS: in-use extent entered DRAINING state"
      ;;
    *)
      fail_to_shell "busy trusted extent did not enter DRAINING state"
      ;;
  esac
fi
if [ "$CVM_TEST_MODE" != smoke ]; then
  for pid in $qemu_pids; do
    kill -CONT "$pid"
  done
fi

for instance in $CVM_GUEST_LABELS; do
  pid=$(guest_pid_for "$instance") ||
    fail_to_shell "missing nested QEMU pid for $instance"
  wait "$pid"
  remember_instance_value "$qemu_rc_table" "$instance" "$?"
done
qemu_pids=

for instance in $CVM_GUEST_LABELS; do
  guest_log=$(guest_log_path "$instance")
  echo "[CVM MULTI] $instance guest log follows"
  cat "$guest_log"
done

validate_guest_log() {
  instance=$1
  log=$2
  status=0
  if [ "$CVM_TEST_MODE" = smoke ]; then
    markers="[CVM INSTANCE] PASS: $instance
[CVM SMOKE] PASS: booted instance $instance"
  else
    markers="[CVM INSTANCE] PASS: $instance
[CVM ENCLAVE] PASS: independent attestation, freshness/replay
[CVM ENCLAVE] PASS: cross-CPU non-enclave random and plugin calls rejected
[CVM ENCLAVE] PASS: parent CVM management calls denied
[CVM ENCLAVE] PASS: two enclaves executed concurrently on CPU0/CPU1
[CVM ENCLAVE] PASS: stale and noncanonical enclave handles rejected after slot reuse
[CVM ENCLAVE] PASS: lifecycle module loaded
[CVM SMP] PASS: cross-CPU wakeup workload completed
[CVM TEST] PASS: SQLite workload completed"
  fi
  while IFS= read -r marker; do
    [ -n "$marker" ] || continue
    if ! grep -Fq "$marker" "$log"; then
      echo "[CVM MULTI] FAIL: $instance log missing marker: $marker"
      status=1
    fi
  done <<EOF
$markers
EOF
  if grep -q '\[CVM[^]]*\] FAIL:' "$log"; then
    echo "[CVM MULTI] FAIL: $instance guest reported a failed regression"
    status=1
  fi
  return "$status"
}

rc=0
for instance in $CVM_GUEST_LABELS; do
  guest_log=$(guest_log_path "$instance")
  guest_rc=$(guest_rc_for "$instance") ||
    fail_to_shell "missing nested QEMU exit status for $instance"
  if [ "$guest_rc" -ne 0 ] ||
     ! validate_guest_log "$instance" "$guest_log"; then
    rc=1
  fi
done
if [ "$rc" -eq 0 ]; then
  if [ "$CVM_TEST_MODE" = smoke ]; then
    echo "[CVM MULTI] PASS: $CVM_GUEST_COUNT CVM smoke guests completed independent boot regressions"
  else
    echo "[CVM MULTI] PASS: $CVM_GUEST_COUNT CVMs completed independent lifecycle regressions"
  fi
else
  echo "[CVM MULTI] FAIL: one or more CVM guests failed"
fi
echo "[CVM] nested QEMUs exited with aggregate status $rc"

echo "[7/8] Shrinking the reclaimed trusted extent"
if ! /cvm/tvm-control shrink "$extent_id"; then
  fail_to_shell "trusted pool shrink failed after CVM teardown"
fi
if /cvm/tvm-control query "$extent_id" >/dev/null 2>&1; then
  fail_to_shell "removed trusted extent is still queryable"
fi

echo "[8/8] Verifying stale extent IDs cannot alias a reused slot"
if ! replacement_output=$(/cvm/tvm-control extend 32768); then
  fail_to_shell "replacement trusted pool extension failed"
fi
echo "$replacement_output"
replacement_id=$(echo "$replacement_output" |
  sed -n 's/^extent_id=\([0-9][0-9]*\)$/\1/p')
case "$replacement_id" in
  ''|*[!0-9]*) fail_to_shell "invalid replacement extent id" ;;
esac
if [ "$replacement_id" -eq "$extent_id" ]; then
  fail_to_shell "reused extent slot returned a stale id"
fi
if /cvm/tvm-control query "$extent_id" >/dev/null 2>&1; then
  fail_to_shell "stale extent id aliases its replacement"
fi
if ! /cvm/tvm-control query "$replacement_id"; then
  fail_to_shell "replacement trusted extent is not queryable"
fi
if ! /cvm/tvm-control shrink "$replacement_id"; then
  fail_to_shell "unused replacement trusted extent could not be removed"
fi
if /cvm/tvm-control query "$replacement_id" >/dev/null 2>&1; then
  fail_to_shell "removed replacement extent is still queryable"
fi
echo "[CVM] PASS: extent generation IDs reject stale handles"

if grep -qw cvm-auto /proc/cmdline; then
  if [ "$rc" -eq 0 ]; then
    echo "[CVM] automatic test completed; powering off host"
  else
    echo "[CVM] automatic test failed; powering off host"
  fi
  sync
  poweroff -f
fi

echo "[CVM] entering recovery shell"
exec setsid sh -c 'exec sh </dev/ttyS0 >/dev/ttyS0 2>&1'
