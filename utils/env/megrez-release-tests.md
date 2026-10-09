# Zion Test Guide

First complete [Deployment](DEPLOYMENT.md).
Defaults: 512 MiB guest RAM, one vCPU, and host SSH forwarding port 10022.
See [Evidence](EVIDENCE.md) for output sources and interpretation.

## 1. Host Initialization

Run as root on the host. Skip initialization if the pool was already successfully
reserved in this host boot. Switching guest systems does not require another reserve.

```sh
cd /root/zion-tests
ls zion-runtime.sh megrez-runtime.conf tvm-driver.ko tvm-control \
   qemu-system-riscv64 guest_kernel_image initrd-linux.img \
   asterinas_kernel initrd-asterinas.img
chmod +x zion-runtime.sh tvm-control qemu-system-riscv64
ls /dev/kvm
sh ./zion-runtime.sh init
rc=$?
printf 'init_exit=%s\n' "$rc"
ls /dev/tvm
```

Expected output includes:

```text
[tvm-control] type=tvm, count=0x30d40
ioctl(): RTVM_IOC_RESERVE_TVM_MEM
[tvm-driver] rtvm_reserve_tvm_mem(): ... device_phys_addr=...
[tvm-driver] reserve TVM SBI result: error=0, value=0
init_exit=0
```

The script loads the driver and reserves 200000 4 KiB pages (about 781 MiB),
as configured. Require a zero script exit and SBI `error=0`.
A loaded driver or `/dev/tvm` alone is insufficient.
If pool state is unknown, inspect prior logs or reboot before initializing;
do not repeatedly reserve.

## 2. Protected Physical Memory

Before starting a CVM:

```sh
sh ./zion-runtime.sh protect
```

`init` saves the actual successful reservation's physical base in
`state/protected-addr`. Do not guess or reuse an address from a previous boot.

Expected host output:

```text
RTVM_IOC_CVM_PHYS_MEMORY_ACCESS: Permission denied
[ZION MEMORY] PASS: host read of 0x... was blocked and recovered
```

The physical console must show a matching monitor record:

```text
[SM] TEE security check: the hypervisor is trying to r/w the protected region
```

The driver uses `copy_from_kernel_nofault()` with exception-table recovery.
Require successful reservation, the actual protected target, recovered access
failure, matching SM output, and continued host/SSH operation without panic.
No successful read value should appear for this access.
Continue with the same trusted pool; do not repeat `init`.

## 3. Select the Guest

```sh
sh ./zion-runtime.sh select
```

Enter `1` for Linux or `2` for Asterinas. The selection is saved in
`state/guest-system` and used by `sh ./zion-runtime.sh guest`.
The explicit `linux` and `asterinas` commands remain available.

Both guests can run vCPU detection, SSH, network/block, and SQLite tests.
Nested enclaves require Linux's `/dev/zion_enclave` frontend.

## 4. Controlled vCPU Tamper Detection

The physical boot console should contain the ZION logo, version, and state:

```text
[ZION BOOT] OpenSBI=1.5 hart=... H=yes
[ZION BOOT] identity=INSECURE-TEST-KEYS not-for-production
[ZION BOOT] state=INITIALIZING (not a security-test PASS)
[ZION BOOT] state=READY CVM/enclave SBI registered; runtime validation still required
```

Perform the injection before other guest tests. Zion prints one alert event
(two lines) per host boot. Restarting the CVM does not reset that alert.

On the host:

```sh
cat /sys/module/kvm/parameters/zion_vcpu_tamper
printf '1\n' > /sys/module/kvm/parameters/zion_vcpu_tamper
sh ./zion-runtime.sh guest
```

The next CVM SBI exit consumes the request and resets the parameter.
Expected host dmesg:

```text
[ZION HOST TEST] controlled-tamper cvm=... vcpu=... register=S3 value=0x5a494f4e; one-shot injection consumed
```

Expected physical-console detection:

```text
[ZION VCPU ALERT] hart=... source=HOST-SHARED-CHANNEL register=S3 expected=0x0 observed=0x5a494f4e
[ZION VCPU ALERT] classification=SUSPECTED-TAMPER-OR-ABI-MISMATCH not proof of malicious intent; private saved state is retained
```

Check on the host:

```sh
dmesg | grep -F '[ZION HOST TEST]'
cat /sys/module/kvm/parameters/zion_vcpu_tamper
```

Require matching S3 values, parameter `0`, and successful subsequent SSH/block
tests. If the parameter remains `1`, inspect guest startup; writing `0` cancels
an unconsumed request. Missing monitor output is not a pass: check firmware
version and whether this boot's one-shot alert was already printed.

## 5. Virtio Network and Block Devices

Reuse the running CVM. If it has not been started:

```sh
sh ./zion-runtime.sh guest
```

Inspect startup without occupying the only serial terminal with `tail -f`:

```sh
sh ./zion-runtime.sh status
```

Expected startup records:

```text
QEMU started (not a Guest PASS). Log: /root/zion-tests/state/cvm.log
Zion CVM SSH is starting on port 22.
Default login: root / debian
```

On the host:

```sh
ssh -p 10022 root@127.0.0.1
```

Password: `debian`. After a verified image-related host-key change:

```sh
ssh-keygen -R '[127.0.0.1]:10022'
ssh -p 10022 root@127.0.0.1
```

Do not blindly dismiss an unexpected identity warning.
Inside the guest:

```sh
uname -a
id
ifconfig eth0
ls -l /dev/vda
dd if=/dev/vda of=/tmp/blk-read bs=512 count=1
rc=$?
printf 'blk_read_exit=%s\n' "$rc"
wc -c /tmp/blk-read
```

Linux uname contains `Linux zion-cvm 6.6.88+`. Asterinas's compatibility output
currently contains `Linux zion-cvm 5.13.0`; confirm its serial boot banner too.
Do not identify the implementation from uname alone.
The default guest address is usually `10.0.2.15`.
Some Asterinas network query ioctls are incomplete and may return
`Inappropriate ioctl for device`; successful data-path checks remain required.

Expected block result:

```text
1+0 records in
1+0 records out
blk_read_exit=0
512 /tmp/blk-read
```

Pass requires successful SSH commands, zero block-read exit, and 512 bytes.
The test disk is host `state/virtio-blk.img` (64 MiB), not the host system disk.

## 6. SQLite

Inside the selected guest:

```sh
/benchmark/bin/sqlite-speedtest1 --size 10 --memdb
rc=$?
printf 'sqlite_exit=%s\n' "$rc"
```

Require the complete workload, `PRAGMA integrity_check`, `TOTAL`, and
`sqlite_exit=0`. This in-memory test does not validate filesystem persistence.

## 7. Nested Enclave (Linux Only)

If Linux is already running, keep the same guest and SSH session.
If Asterinas is running, stop it using the next section, then on the host:

```sh
sh ./zion-runtime.sh enclave
sh ./zion-runtime.sh status
ssh -p 10022 root@127.0.0.1
```

`enclave` is a Linux-mode alias. Do not load Linux modules into Asterinas.
If the image change caused a verified host-key change, remove the old entry:

```sh
ssh-keygen -R '[127.0.0.1]:10022'
ssh -p 10022 root@127.0.0.1
```

The enclave binaries are included in `initrd-linux.img`. Inside Linux:

```sh
/usr/bin/run-zion-enclave-demo
rc=$?
printf 'demo_exit=%s\n' "$rc"
```

Require ten `COMPUTE ... VERIFIED` rounds followed by:

```text
[ZION DEMO] PASS: private checksum continuity across 10 OCALLs; destroyed
[DRIVER SECURITY] PASS: all negative ABI tests
[ZION DEMO] PASS: compute lifecycle and driver negative ABI tests
[ZION DEMO] SKIP: arbitrary private-page access; dedicated safe probe required
demo_exit=0
```

Pass requires all rounds, successful destruction, all negative driver tests,
and exit code zero. The skipped private-page test is not a pass.

## 8. Stop or Switch the Guest

Run `exit` in the guest to return to the host. Disconnecting SSH does not stop
the CVM. Check the recorded PID before stopping it:

```sh
cd /root/zion-tests
pid=$(cat state/cvm.pid)
ps -p "$pid" -o pid,args
sh ./zion-runtime.sh stop
```

The script checks the QEMU PID, waits for exit, and archives the serial log.
If it fails to stop, inspect the process/logs; do not use indiscriminate
`killall qemu` or start another CVM.
Select another system and start `guest` without repeating `init`.

## 9. Save Evidence

| Test | Required evidence |
| --- | --- |
| Memory protection | Actual reserved physical base, attempted target, matching SM record, recovered driver fault, no successful value |
| vCPU detection | Host injection, matching physical-console alert, parameter reset |
| Shared devices | Selected guest, successful SSH, `blk_read_exit=0`, and 512-byte read |
| SQLite | Actual workload/summary and `sqlite_exit=0` |
| Linux enclave | Ten verified rounds, negative-test success, and `demo_exit=0` |

Save the physical console for OpenSBI output, dmesg for host injection/driver
logs, and `state/cvm.log` for guest serial output.
SSH command output is not automatically written to the QEMU serial log.
For a separate enclave transcript, run on the host:

```sh
ssh -p 10022 root@127.0.0.1 \
    '/usr/bin/run-zion-enclave-demo' > state/enclave-ssh.log 2>&1
rc=$?
printf 'ssh_test_exit=%s\n' "$rc"
cat state/enclave-ssh.log
```

Enter the guest password when prompted. Require `ssh_test_exit=0`.
