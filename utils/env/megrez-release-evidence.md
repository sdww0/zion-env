# Test Output and Evidence

Commands are in [Tests](TESTS.md). Correlate all records from the same test run.

## 1. vCPU Shared-Channel Tamper Detection

| Record | Source | Check |
| --- | --- | --- |
| `[ZION HOST TEST] ... register=S3 value=0x5a494f4e` | Host KVM `vcpu_sbi.c`, dmesg | Injects a test value into host-visible shared-channel S3 |
| `[ZION VCPU ALERT] ... register=S3 expected=0x0 observed=0x5a494f4e` | OpenSBI `context.c`, physical console | Detects nonzero shared S3 on CVM SBI return; S3 is not an SBI argument or current MMIO operand |
| Test parameter changes from `1` to `0` | Host sysfs | Confirms consumption of the one-shot request |

Pass requires matching fields/values, parameter reset, and successful subsequent
guest SSH and block reads. This shows detection of the controlled channel change
and continued operation in that run.

The injection does not change the physical CPU's live S3 or the monitor's private
saved state. The alert's `private saved state is retained` describes the handling
path, not a measured guest-private S3 assertion. The test does not verify every
private register. Zion prints one alert event (two lines) per host boot.

## 2. Virtio Network and Block Data Paths

| Result | Source | Check |
| --- | --- | --- |
| SSH login and successful `uname -a` / `id` | Guest commands, SSH terminal | Connection, authentication, and remote execution through this CVM's forwarded port |
| `blk_read_exit=0` | Guest shell after dd | Successful 512-byte read from `/dev/vda` |
| `512 /tmp/blk-read` | Guest wc | Actual read output length |

Pass requires successful SSH commands and a zero-exit block read of 512 bytes.
With the configured `virtio-net-device` and `virtio-blk-device`, this is end-to-end
functional evidence for those data paths.

The test disk is host `state/virtio-blk.img`, not the host system disk.
An UP interface, device node, or Dropbear startup message alone is insufficient.
These results do not independently establish correctness of every monitor shared
mapping or private-page permission.

Run these checks on both guests. Asterinas may report
`Inappropriate ioctl for device` for some interface queries; judge actual SSH
communication and block reads separately.

## 3. Nested Enclave and Driver Constraints (Linux Only)

| Output | Source | Check |
| --- | --- | --- |
| `COMPUTE 1/10 ... VERIFIED` through `10/10` | Guest demo-runner | Recomputes and checks each enclave OCALL step/checksum |
| `shared payload overwritten` | demo-runner OCALL callback | Overwrites the shared payload with 0xa5; later rounds still complete correctly |
| `PASS: private checksum continuity across 10 OCALLs; destroyed` | demo-runner | Ten correct rounds, successful run, zero enclave result, and successful destruction |
| `PASS: cross-file enclave access rejected` | zion-driver-security | Rejects UTM initialization from another file descriptor with EPERM |
| `PASS: out-of-EPM mmap rejected` | zion-driver-security | Rejects an out-of-range EPM mapping with EINVAL |
| `PASS: all negative ABI tests` | zion-driver-security | All implemented negative driver tests succeed |
| `PASS: compute lifecycle and driver negative ABI tests`, `demo_exit=0` | Wrapper and guest shell | Both programs and the wrapper complete successfully |

Pass requires all ten rounds, successful destruction, all driver negative tests,
and exit code zero. This exercises the tested enclave/OCALL lifecycle and driver
constraints inside the CVM.

The parent prints `COMPUTE` after verification; the one-second pause is also in
the parent. Computation, payload overwrite, and driver ABI rejection are not
cryptographic attestation or arbitrary private physical-page isolation tests.
`SKIP: arbitrary private-page access` means that check was not performed.

Asterinas lacks the Linux `/dev/zion_enclave` frontend. Linux results do not imply
Asterinas enclave support.

## 4. Linux/Asterinas SQLite

| Output | Source | Check |
| --- | --- | --- |
| Subtest timings, integrity_check, `TOTAL` | Guest sqlite-speedtest1 | Runs the actual SQLite workload and integrity check |
| `sqlite_exit=0` | Guest shell immediately after SQLite | Process completes successfully |

Require complete workload/summary output and exit code zero. `--memdb` uses an
in-memory database; this does not validate a block filesystem or persistence.
Network and block functionality are tested separately.

## 5. Protected Physical-Memory Read

| Output | Source | Check |
| --- | --- | --- |
| `device_phys_addr=...` | TVM reservation, host log | Actual physical base reserved in this run |
| `reserve TVM SBI result: error=0` | Driver SBI result | Monitor accepts that physical range |
| `Access the physical memory: ... physical address = ...` | Host driver probe | Attempted read targets this run's protected range |
| `protected-memory read blocked: ... fault=-14 (recovered)` | Driver no-fault path, dmesg | Linux exception table catches the forwarded access fault and returns control |
| `[ZION MEMORY] PASS: ... was blocked and recovered` | Host runtime script | Probe fails, recovered-fault marker exists, and execution continues |
| `TEE security check: ... r/w the protected region` plus address/context | OpenSBI `tee-mem.c`, physical console | Protected access enters the monitor security-check path |

Pass requires successful reservation, the actual protected address, a matching SM
record, recovered driver fault, script PASS, and continued host/SSH operation.
There must be no successful `Access the physical memory: value=...` for that read.
Permission numbers, mapping failures, host crashes, or missing output alone are
not a pass.

The probe uses `copy_from_kernel_nofault()` and does not write.
This validates the attempted unauthorized read, not write protection, isolation
of all addresses, or absence of all information leakage.

## Logs and Limits

Monitor output is on the physical console; host injection/driver logs are in
dmesg. Guest serial output is in `state/cvm.log`. Save SSH transcripts separately:
SSH command output is not automatically copied to the guest serial log.
Capture each exit code immediately after its command.

The logo, READY state, QEMU startup message, service startup, or hello text alone
is not a test pass. These are controlled functional experiments, not
cryptographic proofs. QEMU virt results do not replace physical Megrez evidence.
