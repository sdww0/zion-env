# Zion

Zion is an experimental RISC-V security monitor that runs native
Zion/Eyrie enclaves and nested confidential VMs through one OpenSBI
firmware image. The current recovery tree includes generation-safe enclave and
trusted-memory handles, nested enclave lifecycle and crypto services, 16-slot
CVM/enclave capacity with two-CVM full-workload concurrency, complete
per-context RV64D/FCSR isolation, dynamic trusted-memory
contraction, direct-SBI protected-range enforcement, overflow-safe Eyrie edge-call
and CVM management SBI boundaries, overflow-safe Eyrie edge-call and syscall
boundaries, an explicit user-SBI allowlist, fault-recoverable Eyrie
user-memory access,
transactional anonymous-memory syscalls with raw Linux errno and reversible
`PROT_NONE`, bounded vectored IO proxying, and
fault-checked file lifecycle, descriptor/multiplexing, `pselect6`, and
enclave-to-host UDP loopback regressions, plus monotonic time, random, uname,
signal-mask and signal-action registration state, consistent single-thread
process identity, overflow-checked ELF loading with an image-derived program
break, and reproducible nested QEMU inputs.

This repository is an implementation and regression environment, not a
production trust anchor. Its QEMU platform uses deterministic test entropy and
embedded test keys. See [the recovery and security-boundary document](docs/cvm-recovery.md)
for the exact verified scope and remaining production requirements.

## Start here

For Milk-V Megrez hardware bring-up and manual enclave/CVM testing, start with
the dedicated [hardware testing guide](HARDWARE_TESTING.md). It covers the
physical ports, Recovery mount, RAM boot, UART, test order, expected results,
log collection, and recovery after a failed test.

Run the read-only environment diagnostic before building or testing:

```sh
./scripts/doctor.sh build
./scripts/doctor.sh runtime
```

Build the recovered CVM stack with:

```sh
./scripts/build_cvm.sh all
```

Rebuild the native Linux Image and BusyBox initramfs twice in their pinned
cross-compilation container (the second build must be byte-identical):

```sh
JOBS=12 ./scripts/build_native_inputs.sh [/path/to/linux.git]
```

The native SDK, Eyrie runtime, examples, and handle regression can be rebuilt
individually:

```sh
./scripts/build_and_sync.sh sdk
./scripts/build_and_sync.sh eyrie
./scripts/build_and_sync.sh examples
./scripts/build_and_sync.sh native-test
```

When the selected kernel is `build/native-inputs/linux`, driver and regression
modules are compiled automatically with the same recorded OCI compiler as the
kernel; a host compiler with a different version is not mixed into that tree.
Direct module builds first verify every native input against the repository's
reviewed hash manifest, whose trust root cannot be replaced through the ambient
environment.  Each resulting module also has a `.manifest` binding the module
to the current driver sources, prepared kernel ABI files, selected Image, and
native-input manifest; runtime scripts reject a same-release stale module.

## Verification

One entry point covers the supported verification levels:

| Command | What it proves |
| --- | --- |
| `./scripts/verify_zion.sh quick` | Native-input and QEMU hashes, deployed private runtime, production SM strings, linked Eyrie fault-recovery metadata, overflow-safe ELF layout, user-SBI boundaries, artifact-bound logs, and negative edge-call/input/log/path tests |
| `./scripts/verify_zion.sh runtime` | Re-runs native and dual-CVM QEMU regressions, then performs `quick` checks |
| `./scripts/verify_zion.sh full` | Reconstructs pinned sources, requires two byte-identical native-input builds, rebuilds native modules with the recorded compiler, compiles the isolated Eyrie Linux/IO/NET plugin matrix, performs a clean 1458-step QEMU reproducibility build, re-runs both runtime paths, and performs every quick/self-test gate |

For offline source reconstruction, full verification accepts local Git
repositories containing the pinned commits:

```sh
JOBS=12 ./scripts/verify_zion.sh full \
  /path/to/linux-host /path/to/qemu [/path/to/linux-v6.19]
```

The latest successful run writes `build/zion-verification.txt`. Each runtime
log has an adjacent `.artifacts.sha256` manifest, and the manifest digest is
embedded in the log before boot so stale evidence cannot silently validate
changed local artifacts.

## Paths and overrides

Scripts do not require the original developer's absolute directory layout.
They prefer explicit environment variables, then repository-relative
candidates, then tools in `PATH`.

| Variable | Purpose |
| --- | --- |
| `CROSS_COMPILE` | RISC-V compiler prefix, such as `riscv64-linux-gnu-` |
| `QEMU` | Outer `qemu-system-riscv64` executable |
| `NATIVE_KERNEL` | Built native-test RISC-V Linux `Image` |
| `NATIVE_LINUX_SRC` | Configured native kernel tree; its built `Image` is used |
| `ZION_BASE_ROOTFS` | BusyBox/newc base initramfs used for native tests |
| `QEMU_SYSROOT` | RISC-V sysroot, required only for the opt-in `QEMU_BUILD_BACKEND=sysroot` backend |
| `BUILD_DIR` | Build and evidence directory |

The default nested-QEMU and native-input builds use pinned OCI environments.
They do not depend on a developer-specific sysroot or prebuilt native kernel.
After `build_native_inputs.sh`, path discovery prefers
`build/native-inputs/linux` and `build/native-inputs/busybox/rootfs.cpio`;
explicit overrides still win. `doctor.sh runtime` reports the exact resolved
paths before QEMU is started.

## Main directories

- `rockos-opensbi/` — OpenSBI plus the Zion security monitor
- `runtime/`, `sdk/`, `examples/` — Eyrie runtime, application ABI, and tests
- `driver/` — native Zion-compatible host driver
- `deps/tvm-driver/` — CVM host control driver and utility
- `test/` — native and nested runtime regressions
- `scripts/` — builds, source reconstruction, reproducibility, diagnostics,
  and unified verification
- `docs/cvm-recovery.md` — detailed design, evidence, provenance, and limits
