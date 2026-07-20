# Zion

This repository provides the build and runtime integration for the Zion
confidential virtual machine stack on two targets:

- Milk-V Megrez
- QEMU `virt`

The component source trees are maintained as independent Git repositories.
The top-level scripts download the selected public branches, build them in
Podman containers, and collect runtime artifacts under `output/`.

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

Zion changes are already present on these branches. Running `patch.sh` is no
longer required.

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

Review the target device paths in the flashing scripts before running them.
They write directly to removable storage.
