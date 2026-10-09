# Milk-V Megrez Deployment and Runtime Tests

The complete runtime release uses `boot-partition/` and
`root-partition/root/zion-tests/`. It is not a full root filesystem or SD image.
Use an existing bootable RockOS installation; do not format or repartition it.

## Deploy from the Computer

From the extracted release:

```sh
sha256sum -c SHA256SUMS
lsblk -o NAME,SIZE,FSTYPE,LABEL,MOUNTPOINTS,MODEL
sudo ./install-to-device.sh /dev/sdb
```

Replace `/dev/sdb` with the actual disk. Back up its boot files and existing
`/root/zion-tests` first. The installer uses partition 1 for boot and partition 3
for root, overwrites matching boot files, and replaces only `/root/zion-tests`.
For other layouts, follow the release's `docs/DEPLOYMENT.md`.

Firmware is copied as `bootloader.bin`. Copying it to the SD card does not flash
board SPI/eMMC; use the board's previously successful update procedure.
Retain known-booting firmware and recovery instructions. Keep the installed DTB.

Build/sign success is not physical-board boot validation.
The firmware uses insecure experimental keys; the corresponding public key is
in `identity/`. The demo does not perform remote attestation.

## Initialize the Host

After firmware update and a fresh host boot, retain the physical-console log.
Expected monitor startup includes the ZION banner and `[ZION BOOT] state=READY`;
that message alone is not a security-test pass.

On the host:

```sh
cd /root/zion-tests
uname -r
ls /dev/kvm
cat /sys/module/kvm/parameters/zion_vcpu_tamper
chmod +x zion-runtime.sh tvm-control qemu-system-riscv64
sh ./zion-runtime.sh init
sh ./zion-runtime.sh protect
```

Expected host kernel: `6.6.87-win2030`. The tamper parameter defaults to `0`.
Initialize only once per host boot. Require successful SBI reservation,
the actual reserved physical address, a recovered protected-read failure,
matching SM output on the physical console, and continued host operation.

## Linux Tests

On the host:

```sh
printf '1\n' > /sys/module/kvm/parameters/zion_vcpu_tamper
sh ./zion-runtime.sh select linux
sh ./zion-runtime.sh guest
sh ./zion-runtime.sh status
dmesg | grep -F '[ZION HOST TEST]'
cat /sys/module/kvm/parameters/zion_vcpu_tamper
ssh -p 10022 root@127.0.0.1
```

Wait for SSH startup before connecting. Require matching injected/detected
S3=`0x5a494f4e`, one `[ZION VCPU ALERT]` event, and parameter reset to `0`.
Guest password: `debian`.

Inside Linux:

```sh
uname -a
id
dd if=/dev/vda of=/tmp/blk-read bs=512 count=1
printf 'blk_read_exit=%s\n' "$?"
wc -c /tmp/blk-read
/benchmark/bin/sqlite-speedtest1 --size 10 --memdb
printf 'sqlite_exit=%s\n' "$?"
/usr/bin/run-zion-enclave-demo
printf 'demo_exit=%s\n' "$?"
exit
```

Require a 512-byte read and zero exit codes, complete SQLite `TOTAL` output,
ten enclave verified rounds, and successful lifecycle/driver tests.
The arbitrary private-page test is skipped, not passed.

## Asterinas Tests

On the host, after Linux exits:

```sh
sh ./zion-runtime.sh stop
sh ./zion-runtime.sh select asterinas
sh ./zion-runtime.sh guest
sh ./zion-runtime.sh status
ssh -p 10022 root@127.0.0.1
```

Do not initialize the pool again. After a verified image-related host-key change,
run `ssh-keygen -R '[127.0.0.1]:10022'` on the host and reconnect.
The same reminder applies when switching back to Linux for enclave tests.

Inside Asterinas, run the same SSH commands, block read, and SQLite test.
Do not run the enclave test: its frontend is Linux-only.
Exit SSH and run `sh ./zion-runtime.sh stop` on the host.

## Logs and Interpretation

Monitor output: physical serial console. Host injection/driver output: dmesg.
Guest boot/serial output: `state/cvm.log`.
Save SSH transcripts separately; they do not automatically enter the serial log.

Complete releases provide `QUICK_TEST.md`, `docs/TESTS.md`, and
`docs/EVIDENCE.md` with detailed pass criteria and evidence limits.
A firmware-only build directory does not include guest/runtime files; deploy
a complete runtime release to execute these tests.
