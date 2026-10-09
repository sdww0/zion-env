# CVM + Enclave Demonstration

Build with `utils/build-zion-enclave.sh all` in the prepared environment.
This packages `/usr/bin/run-zion-enclave-demo` and static demo/security runners
in `initrd-enclave.img`. The original quick hello test remains unchanged.

## Board usage

Copy `initrd-enclave.img` and `utils/env/tee-enclave-demo.sh` into the existing
Host `/root/zion-tests/` directory alongside the guest kernel. Initialize the
TVM driver and trusted pool once after Host boot. Stop any old CVM occupying
port 10022 before launching the new image. Then on Host:

```sh
cd /root/zion-tests
sh ./tee-enclave-demo.sh
tail -n 60 cvm-demo.log
ssh -p 10022 root@127.0.0.1
```

The Guest password is `debian`. Inside Guest:

```sh
/usr/bin/run-zion-enclave-demo
rc=$?
echo "demo_exit=$rc"
```

The demo performs ten computed-checksum/OCALL rounds. The parent recomputes
each result, overwrites the shared progress payload, waits one second for
visibility, then resumes the enclave. The one-second delay is in the parent,
not proof that the enclave ran for that duration. Final success requires all
ten results and successful enclave destruction. The static driver-security
runner checks malformed requests, descriptor ownership, and mapping bounds.

## Evidence limits

Computation is not cryptographic attestation. Shared-payload overwrite does not
attempt to access arbitrary enclave-private physical memory. Driver ABI denials
are not PMP/G-stage private-page access denials. The script explicitly prints
SKIP for that separate test rather than claiming it passed. A dedicated safe
fault-catching probe must be designed before attempting such access on Megrez.

## OpenSBI diagnostics

The cold-boot banner reports OpenSBI version, available source revision, test-key
mode, and INITIALIZING/READY/DISABLED state. READY is not a runtime test PASS.
The vCPU checker excludes the current MMIO load destination or store operand
because it carries legitimate device data. Saved-channel fields are cleared
before each exit exports its operands, avoiding stale-data alerts. Other
nonzero saved-channel fields emit one
global alert per boot, with hart, register, expected and observed values. The
classification is suspected tampering OR ABI mismatch, not proven malicious
intent. Deliberate fault injection plus private-state validation is a separate
vCPU security test.

Updating the Guest demo initramfs does not require reflashing the bootloader.
The new banner and vCPU diagnostics require newly built OpenSBI; do not replace
the known-booting board image solely to update cosmetic output.
