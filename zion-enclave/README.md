# Zion Enclave Components

This directory contains the enclave components ported to the main Zion tree:

- `driver/kernel/`: Linux enclave driver. Load it with `nested_cvm=1` inside
  a Zion CVM; native REE mode additionally reserves the trusted-memory pool.
- `sdk/`: host, enclave application, edge-call, and attestation libraries.
- `runtime/`: Eyrie enclave runtime and loader.
- `examples/`: enclave applications and host runners.
- `test/`: native and nested lifecycle regression sources.

The security monitor implementation is maintained in `opensbi/zion/`. The
shared SBI function IDs in the SDK and OpenSBI are an ABI and must be changed
together.

Build the nested hello test and an initramfs containing all runtime artifacts:

```sh
./utils/build-zion-enclave.sh all
```

The generated archive is
`output/zion-enclave-build/initrd-enclave.img`. Boot a CVM with that initramfs,
then run:

```sh
run-zion-enclave-test
```

Success is reported as:

```text
[ZION] PASS: hello enclave lifecycle
```

QEMU test firmware is built with deterministic keys and is not suitable for
deployment. Megrez production images require a board-specific key and entropy
provider.
