# Zion Megrez Runtime Package

This package targets Milk-V Megrez with an existing bootable RockOS installation.
It is not a full Linux root filesystem or SD image. Runtime packages contain
binaries, configuration, scripts, the test public key, and guides, not sources,
SDKs, build caches, unpacked initramfs roots, debug ELFs, or build logs.

## Layout

| Location | Contents |
| --- | --- |
| `boot-partition/` | OpenSBI/U-Boot firmware, host kernel/initramfs, and firmware guide |
| `root-partition/root/zion-tests/` | Linux/Asterinas kernels, SSH/SQLite initramfs images, QEMU, driver/control utility, scripts, and configuration |
| `identity/` | Test public key corresponding to the firmware |
| `SHA256SUMS` | File integrity checks |

Keep the system-installed DTB. The host kernel is `6.6.87-win2030`; Linux guest
kernel is `6.6.88+`. The controlled one-shot vCPU test entrypoint is disabled by
default. Check the running host's sysfs parameter rather than inferring support
from a version string alone.

New firmware reports a ZION logo, version, initialization state, and vCPU alerts.
Successful compilation/signing or QEMU virt testing does not establish physical
Megrez boot compatibility. Firmware uses insecure test keys; rebuilding generates
a new experimental device identity. The demo does not perform remote attestation.

## Deploy and Test

From the extracted release:

```sh
sha256sum -c SHA256SUMS
sudo ./install-to-device.sh /dev/sdb
```

Verify the target disk first. The installer updates partition 1 and replaces only
`/root/zion-tests` on partition 3. Follow the established board firmware flashing
procedure; SD card copying is not flashing.

After a fresh host boot, follow `QUICK_TEST.md`.
Detailed instructions are in `docs/DEPLOYMENT.md` and `docs/TESTS.md`;
output interpretation is in `docs/EVIDENCE.md`.

Both Linux and Asterinas support SSH, virtio network/block tests, and SQLite.
Enclave tests require Linux and use binaries already in `initrd-linux.img`.
Initialize the pool once per host boot, stop the CVM before switching kernels,
and do not treat a skipped test or startup banner as a pass.

Scripts resolve paths relative to their own directory and use the packaged QEMU.
The host still needs QEMU's shared libraries. Settings are in
`megrez-runtime.conf`; SSH binds only to host `127.0.0.1:10022`.
Runtime PID files, logs, and the test block disk are generated under `state/`.

## Repackage

From the source workspace:

```sh
bash utils/build-megrez-release.sh /path/to/validated-runtime-release /path/to/uboot-base-release
```

This rebuilds/signs OpenSBI and reuses validated runtime binaries.
It does not rebuild Linux or Asterinas. Output directories must be new.
Firmware intermediates remain outside the deployment package.
