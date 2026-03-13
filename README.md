# Building the Project

## Quick start

This repository prepares the Zion software stack (bootloader, host kernel, guest kernel, TVM driver, and runtime payloads) for two targets: **Milk-V Megrez** and **QEMU `virt`**.

Before you start, make sure the host environment provides:

- `podman` and `podman buildx`
- `git`
- `qemu-user-static` for cross-architecture builds (patched QEMU)
- `sudo` access for the virtual-environment workflow, because `virt/run.sh` mounts `virt/rootfs.ext4`
- QEMU-RISCV **10.1.93** available on the host when launching the virtual environment

If you plan to run the QEMU virtual environment, temporarily comment out the `#ifndef CONFIG_ARCH_ESWIN_EIC770X_SOC_FAMILY` and matching `#endif` in `arch/riscv/kvm/vcpu.c` in the kernel source tree. In the current tree, these are around lines 513 and 515.

### Build Dockerfile

Build the container images used by the helper scripts:

```bash
cd utils/docker
bash ./docker-build.sh
cd ../..
```

The Dockerfiles under `utils/docker/` provide the build environments for the kernel-related steps and the QEMU build.

### Prepare the code

Clone the upstream source trees:

```bash
bash ./download.sh
```

This downloads:

- `opensbi`
- `u-boot`
- `zion-host`
- `zion-guest` (copied from `zion-host`)
- `qemu`

Then apply the repository patches:

```bash
bash ./patch.sh
```

This step applies the local patch sets for OpenSBI, QEMU, and the host kernel tree. Run it only after the source trees have been prepared.

### Build

Start the top-level build:

```bash
bash ./build-all.sh
```

The build flow is split across the helper scripts in `utils/`:

- `build-bootloader.sh`
- `build-kernel.sh`
- `build-qemu.sh`

`utils/common.sh` defines the available build targets and output locations. The stages that actually run are determined by the commands currently enabled in `build-all.sh`. If you want the full stack, make sure the bootloader, kernel, TVM-driver, and QEMU build steps are all enabled in your local build flow.

When all stages are enabled, the `output/` directory contains:

**Bootloader:**

- `fw_payload-megrez.bin` and `fw_payload-megrez.elf`: OpenSBI firmware payload for the Milk-V Megrez target
- `fw_dynamic-qemu.bin`: OpenSBI dynamic firmware for the QEMU `virt` target
- `bootloader_secboot_ddr5_milkv_megrez.bin`: signed Megrez bootloader image

**Linux kernel and TVM driver:**

- `host_kernel_image`: host Linux kernel image
- `guest_kernel_image`: guest Linux kernel image
- `tvm-driver/tvm-control` and `tvm-driver/tvm-driver.ko`: user tool and kernel module used to start the TVM flow

**QEMU:**

- `qemu/`: installed QEMU tree copied from `/usr/local/qemu`

## Run the project

### In Virtual Environment

Use the QEMU-based test environment from the `virt/` directory:

```bash
cd virt
bash ./run.sh
```

Notes:

- `virt/run.sh` rebuilds the project by default, mounts `virt/rootfs.ext4`, and copies the latest guest-kernel, TVM-driver, and QEMU artifacts into the image
- `sudo` is required for the rootfs mount step
- `bash ./run.sh pass-compile` skips the rebuild/rootfs-update phase and launches QEMU directly
- Make sure the BIOS file referenced by `virt/run.sh` matches the OpenSBI/QEMU firmware filename generated in `output/`
- QEMU console and monitor output are multiplexed through the terminal, and the session log is written to `virt/qemu.log`

### In Milkv megrez

Flash the generated artifacts to the target storage, then start the runtime scripts on the board.

Useful reference scripts in this repository:

- `utils/flash_bootloader.sh` or `flash_bootloader.sh`: copy the Megrez bootloader image to removable media
- `flash_to_sdb.sh`: copy the host kernel, guest kernel, TVM driver, and QEMU runtime files to the SD card layout used by the board
- `utils/env/start_tvm_from_boot.sh`: insert the TVM driver and start the TEE VM from the target root filesystem

In practice, the board needs the bootloader, host kernel, guest kernel, TVM driver, and QEMU runtime files placed into the correct partitions before running the Zion test flow.
