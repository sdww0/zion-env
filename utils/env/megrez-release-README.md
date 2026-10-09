# Zion Milk-V Megrez Runtime Release

[Chinese version](README-zh.md)

For Milk-V Megrez with an existing bootable RockOS installation.
Guest login: `root / debian`.

## Guides

- [Quick test](QUICK_TEST.md): run the tests after a fresh host boot.
- [Deployment](docs/DEPLOYMENT.md): copy files to the SD card and initialize the host.
- [Tests](docs/TESTS.md): commands, expected output, and pass criteria.
- [Evidence](docs/EVIDENCE.md): output sources and the scope of each result.
- [Firmware](boot-partition/MEGREZ_DEPLOY_TEST.md): firmware files and update boundaries.

## Contents

| Location | Contents |
| --- | --- |
| `boot-partition/` | Megrez firmware (OpenSBI + U-Boot), host kernel, and host initramfs |
| `root-partition/root/zion-tests/` | Linux/Asterinas kernels, initramfs images, Zion QEMU, TVM driver/control utility, runtime script, and configuration |
| `identity/` | Test device public key corresponding to the firmware |
| `SHA256SUMS` | Integrity checksums for all packaged files |

Deploy from the computer with the SD card attached:

```sh
sudo ./install-to-device.sh /dev/sdb
```

Replace the device with the actual target disk. Without an argument, the installer
prompts with `/dev/sdb` as the default. It overwrites matching boot files and
replaces only `/root/zion-tests` on the root partition.

`initrd-linux.img` includes SSH, enclave module/runtime/tests, and SQLite.
`initrd-asterinas.img` includes SSH and SQLite. Both guests support virtio network,
block-device tests, and SQLite. Nested enclave tests currently require Linux.
Stop the CVM before switching kernels; do not reboot or reserve the host pool again.

## Tests

1. Read the actual protected physical address from the host; the SM blocks the
   access and Linux recovers through its exception table without a host reboot.
2. Inject a controlled vCPU shared-channel modification and observe Zion's
   one-shot alert.
3. Connect to the Linux or Asterinas guest over SSH and read a virtio block device.
4. In Linux, execute ten verified enclave computation/OCALL rounds, destroy the
   enclave, and run driver negative tests. Asterinas does not support this test.
5. Run `sqlite-speedtest1 --size 10 --memdb` inside either guest.

Host kernel: `6.6.87-win2030`. Linux guest kernel: `6.6.88+`.
Resource and port settings are in `megrez-runtime.conf`.
Logs, PID files, and the test disk are created under `state/`.

## Initialization

Run once after each fresh host boot, as root:

```sh
cd /root/zion-tests
chmod +x zion-runtime.sh tvm-control qemu-system-riscv64
sh ./zion-runtime.sh init
sh ./zion-runtime.sh select
```

Skip `init` if initialization already succeeded during this host boot.
Select Linux or Asterinas, then start it with `sh ./zion-runtime.sh guest`.
The explicit `linux` and `asterinas` commands are also available.

Keep the system-installed DTB and existing boot configuration.
Verify `sha256sum -c SHA256SUMS` before deployment.
Firmware uses insecure test keys and is for experiments only.

Keep a known-booting firmware image and the established board recovery procedure.
Copying `bootloader.bin` onto the SD card does not flash board firmware.
Guest or test-script updates do not require reflashing an already matching firmware.

## Rebuilding

From the source workspace, rebuild/sign OpenSBI and package validated runtime inputs:

```sh
bash utils/build-megrez-release.sh /path/to/validated-runtime-release /path/to/uboot-base-release
```

To update guest images or Asterinas separately:

```sh
ZION_RUNTIME_INPUT=/path/to/validated-package \
    bash utils/build-zion-test-initramfs.sh
bash utils/build-asterinas-zion.sh
MEGREZ_RUNTIME_INPUT=/path/to/validated-package \
    bash utils/package-megrez-runtime.sh output/zion-megrez-release-YYYYMMDD
```

Output directories must not already exist. Test rebuilt kernels/images before
packaging, and pass their paths through the documented `MEGREZ_*` overrides;
otherwise packaging reuses the validated package's images.
