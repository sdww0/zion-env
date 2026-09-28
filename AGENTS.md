# AGENTS.md

## Scope

This repository packages the Zion software stack for two targets:

- Milk-V Megrez
- QEMU `virt`

For Trusted Execution Environment work, the default development area is
`opensbi/`, not the Linux trees. Most Zion TEE/security-monitor changes should
land in OpenSBI unless the user explicitly asks for a host/guest kernel change
or an SBI ABI update requires a matching Linux-side change.

When in doubt:

1. Start in `opensbi/zion/`
2. Check the OpenSBI platform hook in `opensbi/platform/generic/`
3. Touch Linux only if the interface or boot flow truly crosses the boundary

Do not spend time refactoring unrelated upstream trees. This workspace contains
large imported projects (`titan-host/`, `zion-host/`, `zion-guest/`, `qemu/`,
`u-boot/`) plus generated artifacts (`output/`, disk images, logs). Ignore
them unless the task directly depends on them.

## Zion/OpenSBI Map

Use this file map before making changes:

- `opensbi/zion/src/zion.c`
  Cold-boot initialization, banner, counter enable, and registration of the
  Zion experimental SBI extension.
- `opensbi/zion/src/tee.h`
  Shared Zion SBI extension ID and function IDs. Treat these as ABI.
- `opensbi/zion/src/tee-sbi-opensbi.c`
  OpenSBI ecall dispatch for the Zion extension.
- `opensbi/zion/src/tee-sbi.c`
  SBI handlers exposed to the REE/TEE side.
- `opensbi/zion/src/tee.c`
  Protected-memory reservation, CVM memory loading, page-table registration,
  and TEE metadata initialization.
- `opensbi/zion/src/cvm.c`
  CVM creation and lifecycle management.
- `opensbi/zion/src/context.c`
  Context save/restore helpers and hart/thread state handoff.
- `opensbi/zion/src/ree.c`
  REE-side metadata and bookkeeping.
- `opensbi/zion/src/sm.c`
  Security-monitor metadata.
- `opensbi/zion/src/pmp.c`
  Runtime PMP layout discovery and protected-region programming.
- `opensbi/zion/src/tee-trap-handler.c`
  Trap/interrupt/MMIO exit handling for CVM execution.
- `opensbi/zion/src/tee-trap.S`
  Low-level trap entry/exit assembly.
- `opensbi/platform/generic/platform.c`
  Calls `zion_init(cold_boot)` from `generic_final_init()`.
- `opensbi/Makefile`
  Pulls `zion/objects.mk` into the OpenSBI build.
- `utils/build-bootloader.sh`
  Project build path for OpenSBI boot artifacts on Megrez and QEMU.

## Project Rules

### 1. Default To OpenSBI

For this project, TEE work is primarily an OpenSBI concern.

- Prefer `opensbi/zion/` over `titan-host/`, `zion-host/`, or `zion-guest/`
- Treat Linux-side code as a consumer of the Zion SBI ABI
- Only edit host/guest kernel code when the task explicitly requires interface
  coordination

### 2. Preserve ABI Contracts

The Zion SBI extension is currently wired through
`SBI_EXT_EXPERIMENTAL_zion` in `opensbi/zion/src/tee.h`.

- Do not change the extension ID casually
- Do not renumber function IDs casually
- If an SBI shape must change, document the Linux/QEMU/U-Boot impact and update
  all affected callers in the same task when possible

### 3. Treat Trap Paths As High Risk

Files in the trap/exit path are security- and correctness-sensitive:

- `opensbi/zion/src/tee-trap-handler.c`
- `opensbi/zion/src/tee-trap.S`
- `opensbi/zion/src/context.c`
- `opensbi/zion/src/tee-sbi.c`

Important invariants:

- `sbi_sm_enter_cvm()` / `sbi_sm_exit_cvm()` transfer control with
  `sbi_trap_exit()` and do not return normally
- `regs->mepc` adjustments are part of the ABI/return path, not cosmetic
- MMIO/trap forwarding to the REE must preserve trap metadata correctly
- Debug prints in hot trap paths should stay minimal

### 4. Treat PMP Changes As Board-Sensitive

`opensbi/zion/src/pmp.c` is not a generic allocator. It reuses OpenSBI's
runtime PMP layout and has logic for fallback layouts on different hardware.

- Do not hard-code PMP entries unless the task proves that is required
- Preserve the cross-hart protection model
- Be careful with `pmp_set_global()` and IPI-based synchronization
- A fix that works on single-hart QEMU may still be wrong on SMP hardware

If you touch PMP behavior, assume Megrez multi-hart behavior matters even when
QEMU looks fine.

### 5. Preserve Boot Ordering

`zion_init()` runs from `opensbi/platform/generic/platform.c`.

- Keep cold-boot-only initialization on the cold-boot hart
- Keep the barrier/wait logic for secondary harts
- Do not move metadata initialization later unless you have checked all call
  sites that assume it already exists

### 6. Keep Changes Narrow

This tree mixes upstream OpenSBI with Zion-specific logic.

- Prefer narrow edits in `opensbi/zion/`
- Avoid broad renames or style-only churn
- Do not reformat unrelated code
- Match the local file's existing structure and naming

## Build And Validation

### Fast OpenSBI-Only Check

Use this when the change is limited to OpenSBI and you want a quick compile
sanity check:

```bash
cd opensbi
make clean
make PLATFORM=generic -j$(nproc)
```

### Project-Style Bootloader Build

This follows the repository's normal OpenSBI packaging path and produces the
boot artifacts used by Megrez/QEMU flows:

```bash
bash ./utils/build-bootloader.sh
```

The helper expects the prepared toolchain/container environment described in the
root `README.md`.

### Full Workspace Build

Use this only when the task actually needs cross-component validation:

```bash
bash ./build-all.sh
```

### QEMU Run Path

For integrated runtime checks:

```bash
cd virt
bash ./run.sh
```

If you only changed OpenSBI internals, a full kernel/QEMU rebuild is usually
not necessary unless the task changes boot artifacts or the external ABI.

### Asterinas Zion Test Artifacts

When updating Asterinas for Zion, always refresh the tested runtime artifacts
under `patch/zion-test-scripts/zion-tests/asterinas-test/` after validation.

Use the fixed podman build wrapper instead of `make kernel` when building the
Zion Asterinas kernel from this workspace:

```bash
bash ./utils/build-asterinas-zion.sh
```

For the combined Megrez runtime tests, prepare Linux images with
`bash ./utils/build-zion-test-initramfs.sh`. Linux uses one `initrd-linux.img`
for SSH, virtio and enclave tests. Asterinas uses the tested SSH initramfs from
`patch/zion-test-scripts/zion-tests/asterinas-test/asterinas_initramfs.cpio.gz`
for SSH, virtio-net, virtio-blk and SQLite. Zion enclave remains Linux-only.
Switching kernels requires stopping the CVM, not rebooting the Host or reserving
the pool again. Build with podman
image `docker.io/asterinas/asterinas:0.18.0-20260701` (the wrapper default).

The wrapper reuses the tested Zion initramfs from
`patch/zion-test-scripts/zion-tests/asterinas-test/`, avoids Asterinas's
container-sensitive initramfs generation step, and syncs the raw RISC-V kernel
binary back into the Zion test-artifact directory. It keeps its cargo and
rustup caches under `output/asterinas-build-cache/`, which is intentionally
ignored by git.

- Copy the tested raw RISC-V kernel binary to
  `patch/zion-test-scripts/zion-tests/asterinas-test/aster-kernel-osdk-bin`.
- Keep `patch/zion-test-scripts/zion-tests/asterinas-test/asterinas_kernel`
  as the same binary for compatibility with older scripts.
- Do not use `aster-kernel-osdk-bin.qemu_elf` as the default `-kernel` input
  for these scripts; it can overlap with ELF-loaded program segments.
- If the initramfs changes, update both
  `patch/zion-test-scripts/zion-tests/asterinas-test/asterinas_initramfs.cpio.gz`
  and the packaged `initrd-asterinas.img` only after testing SSH, virtio and
  SQLite on Zion.
- The default Asterinas SSH test path should remain
  `patch/zion-test-scripts/zion-tests/asterinas-test/start_tvm_asterinas_from_boot.sh`.

## Target-Specific Notes

Keep both supported targets in mind:

- Megrez build uses OpenSBI firmware payload flow plus signing
- QEMU `virt` uses the generic-platform OpenSBI flow and `fw_dynamic`

A change in platform init, PMP layout, hart synchronization, or trap handling
should be reviewed with both targets in mind. QEMU convenience does not
override board correctness.

## Style

Follow the existing OpenSBI/Zion style.

- Respect `opensbi/.clang-format`
- Use tabs; indentation width is 8
- Do not sort includes automatically
- Keep comments short and high-signal
- Prefer existing OpenSBI helpers/types/macros over introducing new utility
  layers

## Practical Workflow

For most Zion TEE tasks, this order works well:

1. Read the relevant files in `opensbi/zion/src/`
2. Confirm whether the change is ABI-facing, trap-path-facing, or PMP-facing
3. Make the smallest possible OpenSBI change first
4. Run an OpenSBI-focused build check
5. Only then expand to workspace-wide validation if the task actually crosses
   component boundaries

## Dirty Tree Guidance

This repository may contain local edits, generated outputs, large images, and
logs.

- Do not delete unrelated files
- Do not revert user changes outside your task
- Avoid touching generated artifacts unless the user asks for packaging/output
  updates
