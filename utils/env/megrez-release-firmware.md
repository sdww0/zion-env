# Megrez Firmware and Boot Files

| File | Purpose |
| --- | --- |
| `bootloader_secboot_ddr5_milkv_megrez.bin` | nsign-packaged OpenSBI + U-Boot firmware; copy to the SD card as `bootloader.bin` |
| `vmlinuz-6.6.87-win2030` | Host Linux with the controlled one-shot vCPU tamper test entrypoint, disabled by default |
| `initrd.img-6.6.87-win2030` | Matching host initramfs; used in virt, but board storage compatibility must be checked |

Firmware reports the ZION logo, OpenSBI version, initialization state, and a
one-shot vCPU alert. Keep the system-installed DTB; no replacement is required.
After updating the host kernel, check
`/sys/module/kvm/parameters/zion_vcpu_tamper`.

- [SD card deployment and initialization](../docs/DEPLOYMENT.md)
- [Test commands and expected output](../docs/TESTS.md)
- [Output sources and evidence](../docs/EVIDENCE.md)
- [Runtime release overview](../README.md)

Keep known-booting firmware. Verify the target disk and partitions before
overwriting files; do not reuse hard-coded device names without checking them.
This package does not format disks, create partitions, or flash board SPI/eMMC.
Copying firmware to the SD card is not a board firmware update.
Use the board's established update procedure, not unverified raw-write commands.
