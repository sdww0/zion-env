# Zion Megrez Deployment

Run steps 1-5 on the computer with the SD card attached. Run step 6 on Megrez.
The SD card must already contain bootable RockOS, and the host must provide
the shared libraries required by QEMU.

## 1. Extract and Verify

```sh
tar -xzf zion-megrez-release-YYYYMMDD.tar.gz
cd zion-megrez-release-YYYYMMDD
sha256sum -c SHA256SUMS
```

Use the actual archive name and extraction command for its format.
Every checksum must report `OK`.

For automated deployment, use `sudo ./install-to-device.sh /dev/sdb` with the
actual target disk. It uses partitions 1 and 3, renames the firmware to
`bootloader.bin`, and replaces only `/root/zion-tests` on partition 3.
The remaining steps describe manual deployment for other partition layouts.

## 2. Identify and Mount the SD Card

Shut down the board before removing its system card.

```sh
lsblk -o NAME,SIZE,FSTYPE,LABEL,MOUNTPOINTS,MODEL
```

Replace `sdX1` and `sdXN` with the actual boot and root partitions.
Do not format or repartition the card. Unmount automatically mounted partitions
before mounting them here.

```sh
BOOT_DEV=/dev/sdX1
ROOT_DEV=/dev/sdXN
BOOT_MNT=/mnt/zion-megrez-boot
ROOT_MNT=/mnt/zion-megrez-root
sudo mkdir -p "$BOOT_MNT" "$ROOT_MNT"
sudo mount "$BOOT_DEV" "$BOOT_MNT"
sudo mount "$ROOT_DEV" "$ROOT_MNT"
findmnt "$BOOT_MNT"
findmnt "$ROOT_MNT"
sudo ls "$BOOT_MNT"
sudo ls "$ROOT_MNT"
```

The boot partition must contain the existing boot files; the root partition
must contain directories such as `etc`, `usr`, and `root`.

## 3. Back Up Existing Files

Store backups on the computer, not just on the same SD card:

```sh
BACKUP="$HOME/megrez-sd-backup-$(date +%Y%m%d-%H%M%S)"
mkdir -p "$BACKUP"
sudo cp -a "$BOOT_MNT" "$BACKUP/boot-partition"
if sudo test -d "$ROOT_MNT/root/zion-tests"; then
    sudo cp -a "$ROOT_MNT/root/zion-tests" "$BACKUP/zion-tests"
fi
```

This backs up SD card files, not firmware already flashed to board SPI/eMMC.
Also retain known-booting firmware and the board recovery procedure.

## 4. Copy Boot Files

```sh
sudo install -m 644 boot-partition/bootloader_secboot_ddr5_milkv_megrez.bin \
    "$BOOT_MNT/bootloader.bin"
sudo install -m 644 boot-partition/vmlinuz-6.6.87-win2030 "$BOOT_MNT/"
sudo install -m 644 boot-partition/initrd.img-6.6.87-win2030 "$BOOT_MNT/"
```

Keep the existing boot configuration, root arguments, and system-installed DTB.
Check that the configuration selects these kernel/initramfs files.
The host initramfs has been used in virt; verify its board storage compatibility.
Copying firmware is not flashing it. Use the board's previously successful
firmware update procedure.

## 5. Copy CVM Runtime Files

Use a fresh directory to avoid mixing old images, binaries, or scripts:

```sh
if sudo test -e "$ROOT_MNT/root/zion-tests"; then
    sudo mv "$ROOT_MNT/root/zion-tests" \
        "$ROOT_MNT/root/zion-tests.previous-$(date +%Y%m%d-%H%M%S)"
fi
sudo cp -a root-partition/root/zion-tests "$ROOT_MNT/root/"
sudo chown -R root:root "$ROOT_MNT/root/zion-tests"
sync
sudo umount "$ROOT_MNT"
sudo umount "$BOOT_MNT"
```

Remove the card only after both unmount operations succeed.
Reinsert it into Megrez and boot the host.

## 6. Check and Initialize the Host

```sh
cd /root/zion-tests
uname -r
ls /dev/kvm
cat /sys/module/kvm/parameters/zion_vcpu_tamper
```

Expected host version: `6.6.87-win2030`. The test parameter defaults to `0`.
A missing parameter means the running kernel does not expose the test entrypoint.

If the TVM driver and trusted pool have not been initialized during this boot:

```sh
chmod +x zion-runtime.sh tvm-control qemu-system-riscv64
sh ./zion-runtime.sh init
rc=$?
printf 'init_exit=%s\n' "$rc"
ls /dev/tvm
```

Require `RTVM_IOC_RESERVE_TVM_MEM`, `device_phys_addr=...`,
`reserve TVM SBI result: error=0`, and `init_exit=0`.
The presence of `/dev/tvm` alone does not establish pool initialization.
Initialize only once per host boot, including when switching guest systems.

```sh
sh ./zion-runtime.sh select
```

Follow [Tests](TESTS.md) or the release's `QUICK_TEST.md`.
Both guests use `root / debian`. SSH forwarding binds only to host
`127.0.0.1:10022`. Enclave tests require Linux.
