# Zion Quick Test

Start from a freshly booted Megrez host. Guest login: `root / debian`.
Keep the physical serial console connected to capture OpenSBI/Zion output.
Initialize the trusted pool only once during this host boot.

## 1. Initialize and Test Memory Protection

On the host:

```sh
cd /root/zion-tests
chmod +x zion-runtime.sh tvm-control qemu-system-riscv64
sh ./zion-runtime.sh init
sh ./zion-runtime.sh protect
```

Required output:

```text
reserve TVM SBI result: error=0, value=0
[ZION MEMORY] PASS: host read of 0x... was blocked and recovered
```

The physical console must also show the matching access:

```text
[SM] TEE security check: the hypervisor is trying to r/w the protected region
```

The host must remain running and reachable over SSH. Do not run `init` again.

## 2. Linux: vCPU Detection, Shared Devices, SQLite, and Enclave

On the host:

```sh
cd /root/zion-tests
printf '1\n' > /sys/module/kvm/parameters/zion_vcpu_tamper
sh ./zion-runtime.sh select linux
sh ./zion-runtime.sh guest
sh ./zion-runtime.sh status
```

Repeat `status` until the guest SSH startup message appears.
Require one matching `[ZION VCPU ALERT]` on the physical console.

```sh
dmesg | grep -F '[ZION HOST TEST]'
cat /sys/module/kvm/parameters/zion_vcpu_tamper
```

The parameter must return to `0`. Connect to the guest:

```sh
ssh -p 10022 root@127.0.0.1
```

If a known image replacement changed its host key, remove the old entry and retry.
Do not remove an unexpected key warning without verifying the cause:

```sh
ssh-keygen -R '[127.0.0.1]:10022'
ssh -p 10022 root@127.0.0.1
```

Inside the Linux guest:

```sh
uname -a
ifconfig eth0
dd if=/dev/vda of=/tmp/blk-read bs=512 count=1
printf 'blk_read_exit=%s\n' "$?"
wc -c /tmp/blk-read
/benchmark/bin/sqlite-speedtest1 --size 10 --memdb
printf 'sqlite_exit=%s\n' "$?"
/usr/bin/run-zion-enclave-demo
printf 'demo_exit=%s\n' "$?"
exit
```

Require zero exit codes, a 512-byte block read, SQLite `TOTAL`, ten enclave
`VERIFIED` rounds, and the computation/driver/wrapper PASS results.
`SKIP` is not a pass. Back on the host:

```sh
sh ./zion-runtime.sh stop
```

## 3. Asterinas: Shared Devices and SQLite

Continue in the same host boot without reserving the pool again:

```sh
cd /root/zion-tests
sh ./zion-runtime.sh select asterinas
sh ./zion-runtime.sh guest
sh ./zion-runtime.sh status
ssh -p 10022 root@127.0.0.1
```

If the guest image change caused a verified SSH host-key change, run
`ssh-keygen -R '[127.0.0.1]:10022'` on the host before reconnecting.

Inside Asterinas:

```sh
uname -a
ifconfig eth0
dd if=/dev/vda of=/tmp/blk-read bs=512 count=1
printf 'blk_read_exit=%s\n' "$?"
wc -c /tmp/blk-read
/benchmark/bin/sqlite-speedtest1 --size 10 --memdb
printf 'sqlite_exit=%s\n' "$?"
exit
```

Require interactive SSH, zero exit codes, a 512-byte read, and SQLite `TOTAL`.
Some network queries may return `Inappropriate ioctl for device`; assess actual
SSH communication separately. Enclave tests are Linux-only.

Finally, on the host:

```sh
sh ./zion-runtime.sh stop
```

Guest logs are under `/root/zion-tests/state/`. Save SSH transcripts separately.
See `docs/TESTS.md` and `docs/EVIDENCE.md` for detailed checks and evidence limits.
