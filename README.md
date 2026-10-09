# Zion

[Chinese version](README-zh.md)

This repository provides build and runtime integration for the Zion confidential
virtual machine stack on Milk-V Megrez and QEMU `virt`. Component source trees
are maintained as independent Git repositories. The top-level scripts download
public branches, build them in Podman containers, and collect runtime artifacts
under `output/`.

## One-Command Workflows

Run the commands below from the workspace root. Builds use Podman. Component
sources are independent Git repositories; build caches, images, and release
binaries are not committed to this repository.

| Task | Command |
| --- | --- |
| Download component sources | `bash ./download.sh` |
| Prepare build containers | `bash ./utils/docker/docker-build.sh` |
| Build OpenSBI, Linux, and QEMU | `bash ./build-all.sh` |
| Build Asterinas and sync test binaries | `bash ./utils/build-asterinas-zion.sh` |
| Preview cache cleanup | `bash ./utils/clean-cache.sh` |
| Remove selected caches | `bash ./utils/clean-cache.sh --apply` |

With a validated runtime package and a base package containing U-Boot, rebuild
OpenSBI, sign the firmware, and assemble a timestamped release in one command:

```bash
bash ./utils/build-megrez-release.sh /path/to/validated-runtime-release /path/to/uboot-base-release
```

The base package must contain `boot/u-boot-megrez.bin` and
`boot/u-boot-megrez.dtb`. The runtime package must provide `SHA256SUMS`. This
step reuses its validated host/guest kernels, initramfs images, drivers, and
QEMU; it does not rebuild them. Build records remain in
`output/megrez-firmware-*`. Releases under `output/zion-megrez-release-*`
contain only runtime files and documentation. Randomly generated test keys
are experimental identities, not production security credentials.

Deploy the release to an SD card, replacing the path and device as appropriate:

```bash
sudo bash /path/to/release/install-to-device.sh /dev/sdb
```

The installer overwrites matching files on partition 1 and names the firmware
`bootloader.bin`. On partition 3, it replaces only `/root/zion-tests`, not the
entire partition. Copying firmware to the boot partition does not flash the
board. After flashing through the board's established procedure and booting
host Linux, follow the release's `QUICK_TEST.md` for initialization and tests.
Nested enclaves currently require a Linux guest; Asterinas is not supported
for enclave tests.

Cache cleanup preserves toolchains, downloaded dependencies, releases, images,
and logs, and skips mounted directories. First-time setup and component build
instructions follow below.

## Prerequisites

Install the following tools on the build host:

- Git
- Podman
- `qemu-user-static` and binfmt support when building the RISC-V QEMU
  container on a non-RISC-V host
- `sudo` for the `virt/run.sh` root filesystem update path

All component repositories are accessed through HTTPS. SSH Git credentials are
not required.

## 1. Download the sources

Run the download script from anywhere:

```bash
./download.sh
```

It prepares the following repositories with `--single-branch` semantics:

| Directory | Repository | Branch |
| --- | --- | --- |
| `opensbi/` | `https://github.com/sdww0/zion.git` | `main` |
| `u-boot/` | `https://github.com/sdww0/rockos-u-boot.git` | `rockos-v2024.01` |
| `zion-host/` | `https://github.com/sdww0/rockos-kernel.git` | `zion-host` |
| `zion-guest/` | `https://github.com/sdww0/rockos-kernel.git` | `zion-guest` |
| `qemu/` | `https://github.com/sdww0/qemu.git` | `zion-qemu` |

Existing clean repositories are fetched and fast-forwarded. The script stops
if a repository has tracked local changes or its local branch has diverged; it
never resets local work. Untracked build outputs are left untouched.

## 2. Build the container images

The source build uses separate amd64 kernel and riscv64 QEMU images:

```bash
cd utils/docker
./docker-build.sh
cd ../..
```

The default image names are:

- `localhost/zion-kernel:0.1.0`
- `localhost/zion-qemu:0.1.0`

The image builder first pulls the amd64 and riscv64 Ubuntu base images from
the registry. If a pull fails, it falls back to the matching
`ubuntu-24.04-*.tar` archive in `utils/docker/`, which keeps offline builds
available.

## 3. Build Zion

Build the complete stack with a separate one-shot command:

```bash
./build-all.sh
```

The build runs in disposable Podman containers and removes them automatically.
To build only one side of the stack, use:

```bash
./build-all.sh --kernel
./build-all.sh --qemu
```

Custom container image names can be supplied through `ZION_KERNEL_IMAGE` and
`ZION_QEMU_IMAGE`.

The build invokes these component helpers:

- `utils/build-bootloader.sh`
- `utils/build-kernel.sh`
- `utils/build-qemu.sh`

Successful builds place the main artifacts under `output/`:

- `fw_payload-megrez.bin` and `fw_payload-megrez.elf`
- `fw_dynamic-qemu.bin`
- `bootloader_secboot_ddr5_milkv_megrez.bin`
- `host_kernel_image`
- `guest_kernel_image`
- `tvm-driver/tvm-control`
- `tvm-driver/tvm-driver.ko`
- `qemu/`

## Run with QEMU `virt`

```bash
cd virt
./run.sh
```

`virt/run.sh` normally rebuilds the stack, updates `virt/rootfs.ext4`, and then
starts QEMU. To reuse the current artifacts and skip rebuilding:

```bash
./run.sh pass-compile
```

The console is attached to the current terminal and written to `virt/qemu.log`.

## Run on Milk-V Megrez

After building, copy the bootloader, host kernel, guest kernel, TVM driver, and
QEMU runtime to the board storage layout. The relevant helpers are:

- `utils/flash_bootloader.sh`
- `utils/flash_to_sdb.sh`
- `utils/env/start_tvm_from_boot.sh`

For bootloader-only copying, explicitly select the boot partition:

```bash
sudo bash ./utils/flash_bootloader.sh /dev/sdb1
```

Prefer the release installer above for a complete deployment. Both entrypoints
require confirmation before writing to removable storage.
