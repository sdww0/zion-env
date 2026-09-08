# Zion Enclave Driver

This is a loadable kernel module for Zion Enclave.  To build the
module, make with the top-level
[Zion](https://github.com/zion-enclave/zion) build
process.

# Compatibility

The driver will always work correctly with the version of riscv-linux
pointed to by the top-level
[Zion](https://github.com/zion-enclave/zion) repository.

For the upstream linux, loadable modules for RISC-V only work on kernel versions later than 4.17.

Both `/dev/zion_enclave` and the unified driver's `/dev/zion_cvm` are created with
mode `0600`.  Deployments that need non-root enclave clients should grant a
dedicated group access with a udev rule rather than making either management
device world writable.

The legacy TVM raw-address debug ioctls are not compiled by default.  A
disposable test-only module can opt in with `ZION_UNSAFE_DEBUG_IOCTLS=1`; even
then the caller must hold `CAP_SYS_RAWIO`.  Such a module must not be included
in production images.

To use the module in 4.15, please use this version

https://github.com/riscv/riscv-linux/tree/65e929792fb9b632c20be118fa0795b26cc89fe4

If you are using kernel earlier than 4.15, you might need to apply Zong's patch by yourself.

https://lore.kernel.org/patchwork/patch/933133/
