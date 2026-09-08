#!/usr/bin/env bash
# Prove that the runtime-log gate rejects incomplete logs, repeated freshness
# challenges, missing OCALL evidence, fatal markers, and a manifest that is not
# the one bound to a log.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ZION_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ZION_DIR/build}"
VERIFIER="$ZION_DIR/scripts/verify_runtime_log.sh"
CVM_LOG="${CVM_RUN_LOG:-$BUILD_DIR/cvm-qemu-last.log}"
NATIVE_LOG="${ZION_RUN_LOG:-$BUILD_DIR/zion-qemu.log}"
CVM_MANIFEST="${CVM_ARTIFACT_MANIFEST:-$CVM_LOG.artifacts.sha256}"
NATIVE_MANIFEST="${ZION_ARTIFACT_MANIFEST:-$NATIVE_LOG.artifacts.sha256}"

fail() {
	echo "[FAIL] $*" >&2
	exit 1
}

[ -x "$VERIFIER" ] || fail "missing runtime log verifier: $VERIFIER"
for file in "$CVM_LOG" "$NATIVE_LOG" "$CVM_MANIFEST" "$NATIVE_MANIFEST"; do
	[ -f "$file" ] || fail "missing verifier fixture: $file"
done

selftest_tmp="$(mktemp -d "${TMPDIR:-/tmp}/zion-log-selftest.XXXXXX")"
cleanup() {
	case "$selftest_tmp" in
		"${TMPDIR:-/tmp}"/zion-log-selftest.*)
			find "$selftest_tmp" -depth -delete
			;;
		*) echo "[WARN] refusing to remove unexpected path: $selftest_tmp" >&2 ;;
	esac
}
trap cleanup EXIT

expect_reject() {
	local label="$1"
	shift
	if "$@" >/dev/null 2>&1; then
		fail "runtime log verifier accepted negative case: $label"
	fi
	echo "[RUNTIME LOG SELFTEST] PASS: $label rejected"
}

remaining_after_deleting_one() {
	local marker="$1"
	local count
	count="$(grep -Fc "$marker" "$CVM_LOG")"
	[ "$count" -gt 1 ] || fail "could not count repeated CVM marker: $marker"
	printf '%s\n' "$((count - 1))"
}

# First ensure these are valid positive fixtures. Otherwise a negative result
# below would say nothing about the mutation being tested.
"$VERIFIER" cvm "$CVM_LOG" "$CVM_MANIFEST" >/dev/null
"$VERIFIER" native "$NATIVE_LOG" "$NATIVE_MANIFEST" >/dev/null

missing_sqlite="$selftest_tmp/cvm-missing-sqlite.log"
cp "$CVM_LOG" "$missing_sqlite"
sed -i '0,/\[CVM TEST\] PASS: SQLite workload completed/{
	/\[CVM TEST\] PASS: SQLite workload completed/d
}' "$missing_sqlite"
[ "$(grep -Fc '[CVM TEST] PASS: SQLite workload completed' \
	"$missing_sqlite")" -eq "$(remaining_after_deleting_one '[CVM TEST] PASS: SQLite workload completed')" ] ||
	fail "could not construct SQLite fixture"
expect_reject "missing primary SQLite marker" \
	"$VERIFIER" cvm "$missing_sqlite" "$CVM_MANIFEST"

missing_nested_fp="$selftest_tmp/cvm-missing-nested-fp.log"
cp "$CVM_LOG" "$missing_nested_fp"
sed -i '0,/\[CVM ENCLAVE\] PASS: fresh and concurrent FP\/FCSR state survived timer and service transitions/{
	/\[CVM ENCLAVE\] PASS: fresh and concurrent FP\/FCSR state survived timer and service transitions/d
}' "$missing_nested_fp"
[ "$(grep -Fc '[CVM ENCLAVE] PASS: fresh and concurrent FP/FCSR state survived timer and service transitions' \
	"$missing_nested_fp")" -eq "$(remaining_after_deleting_one '[CVM ENCLAVE] PASS: fresh and concurrent FP/FCSR state survived timer and service transitions')" ] ||
	fail "could not construct nested FP fixture"
expect_reject "missing primary nested FP marker" \
	"$VERIFIER" cvm "$missing_nested_fp" "$CVM_MANIFEST"

missing_parent_fp="$selftest_tmp/cvm-missing-parent-fp.log"
cp "$CVM_LOG" "$missing_parent_fp"
sed -i '0,/\[CVM ENCLAVE\] PASS: concurrent parent CVM FP\/FCSR guards survived nested transitions/{
	/\[CVM ENCLAVE\] PASS: concurrent parent CVM FP\/FCSR guards survived nested transitions/d
}' "$missing_parent_fp"
[ "$(grep -Fc '[CVM ENCLAVE] PASS: concurrent parent CVM FP/FCSR guards survived nested transitions' \
	"$missing_parent_fp")" -eq "$(remaining_after_deleting_one '[CVM ENCLAVE] PASS: concurrent parent CVM FP/FCSR guards survived nested transitions')" ] ||
	fail "could not construct parent FP fixture"
expect_reject "missing primary parent CVM FP marker" \
	"$VERIFIER" cvm "$missing_parent_fp" "$CVM_MANIFEST"

missing_cross_vcpu="$selftest_tmp/cvm-missing-cross-vcpu.log"
cp "$CVM_LOG" "$missing_cross_vcpu"
sed -i '0,/\[CVM ENCLAVE\] PASS: one enclave resumed across CPU0\/CPU1 with parent FP\/FCSR guards intact/{
	/\[CVM ENCLAVE\] PASS: one enclave resumed across CPU0\/CPU1 with parent FP\/FCSR guards intact/d
}' "$missing_cross_vcpu"
[ "$(grep -Fc '[CVM ENCLAVE] PASS: one enclave resumed across CPU0/CPU1 with parent FP/FCSR guards intact' \
	"$missing_cross_vcpu")" -eq "$(remaining_after_deleting_one '[CVM ENCLAVE] PASS: one enclave resumed across CPU0/CPU1 with parent FP/FCSR guards intact')" ] ||
	fail "could not construct cross-vCPU fixture"
expect_reject "missing primary cross-vCPU resume marker" \
	"$VERIFIER" cvm "$missing_cross_vcpu" "$CVM_MANIFEST"

missing_cvm_management="$selftest_tmp/cvm-missing-management.log"
cp "$CVM_LOG" "$missing_cvm_management"
sed -i '/\[CVM MANAGEMENT\] PASS: protected mappings, page-table injection, non-leaf load, and overflow rejected/d' \
	"$missing_cvm_management"
if grep -Fq '[CVM MANAGEMENT] PASS: protected mappings, page-table injection, non-leaf load, and overflow rejected' \
	"$missing_cvm_management"; then
	fail "could not construct CVM management fixture"
fi
expect_reject "missing CVM management isolation marker" \
	"$VERIFIER" cvm "$missing_cvm_management" "$CVM_MANIFEST"

missing_cvm_driver_security="$selftest_tmp/cvm-missing-driver-security.log"
cp "$CVM_LOG" "$missing_cvm_driver_security"
sed -i '/\[CVM DRIVER\] PASS: root-only device and unsafe debug ioctls disabled/d' \
	"$missing_cvm_driver_security"
if grep -Fq '[CVM DRIVER] PASS: root-only device and unsafe debug ioctls disabled' \
	"$missing_cvm_driver_security"; then
	fail "could not construct CVM driver-security fixture"
fi
expect_reject "missing CVM driver-security marker" \
	"$VERIFIER" cvm "$missing_cvm_driver_security" "$CVM_MANIFEST"

primary_line="$(tr -d '\r' < "$CVM_LOG" |
	grep -F '[CVM] primary verifier challenges:')"
[ -n "$primary_line" ] || fail "could not read primary verifier challenges"
repeated_line="${primary_line/primary/secondary}"
repeated_challenge="$selftest_tmp/cvm-repeated-challenge.log"
cp "$CVM_LOG" "$repeated_challenge"
sed -i "s|^\[CVM\] secondary verifier challenges:.*|$repeated_line|" \
	"$repeated_challenge"
expect_reject "repeated verifier challenges" \
	"$VERIFIER" cvm "$repeated_challenge" "$CVM_MANIFEST"

missing_native="$selftest_tmp/native-missing-hello.log"
cp "$NATIVE_LOG" "$missing_native"
sed -i '/^\[PASS\] hello\r*$/d' "$missing_native"
if grep -Fq '[PASS] hello' "$missing_native"; then
	fail "could not construct native marker fixture"
fi
expect_reject "missing native hello marker" \
	"$VERIFIER" native "$missing_native" "$NATIVE_MANIFEST"

missing_linux_memory="$selftest_tmp/native-missing-linux-memory.log"
cp "$NATIVE_LOG" "$missing_linux_memory"
sed -i '/^\[PASS\] linux-memory\r*$/d' "$missing_linux_memory"
if grep -Fq '[PASS] linux-memory' "$missing_linux_memory"; then
	fail "could not construct native Linux-memory fixture"
fi
expect_reject "missing native Linux-memory marker" \
	"$VERIFIER" native "$missing_linux_memory" "$NATIVE_MANIFEST"

missing_linux_abi="$selftest_tmp/native-missing-linux-abi.log"
cp "$NATIVE_LOG" "$missing_linux_abi"
sed -i '/^\[PASS\] linux-abi\r*$/d' "$missing_linux_abi"
if grep -Fq '[PASS] linux-abi' "$missing_linux_abi"; then
	fail "could not construct native Linux-ABI fixture"
fi
expect_reject "missing native Linux-ABI marker" \
	"$VERIFIER" native "$missing_linux_abi" "$NATIVE_MANIFEST"

missing_io_vector="$selftest_tmp/native-missing-io-vector.log"
cp "$NATIVE_LOG" "$missing_io_vector"
sed -i '/^\[PASS\] io-vector\r*$/d' "$missing_io_vector"
if grep -Fq '[PASS] io-vector' "$missing_io_vector"; then
	fail "could not construct native vectored-IO fixture"
fi
expect_reject "missing native vectored-IO marker" \
	"$VERIFIER" native "$missing_io_vector" "$NATIVE_MANIFEST"

missing_io_file="$selftest_tmp/native-missing-io-file.log"
cp "$NATIVE_LOG" "$missing_io_file"
sed -i '/^\[PASS\] io-file\r*$/d' "$missing_io_file"
if grep -Fq '[PASS] io-file' "$missing_io_file"; then
	fail "could not construct native file-IO fixture"
fi
expect_reject "missing native file-IO marker" \
	"$VERIFIER" native "$missing_io_file" "$NATIVE_MANIFEST"

missing_io_multiplex="$selftest_tmp/native-missing-io-multiplex.log"
cp "$NATIVE_LOG" "$missing_io_multiplex"
sed -i '/^\[PASS\] io-multiplex\r*$/d' "$missing_io_multiplex"
if grep -Fq '[PASS] io-multiplex' "$missing_io_multiplex"; then
	fail "could not construct native multiplexed-IO fixture"
fi
expect_reject "missing native multiplexed-IO marker" \
	"$VERIFIER" native "$missing_io_multiplex" "$NATIVE_MANIFEST"

missing_net_loopback="$selftest_tmp/native-missing-net-loopback.log"
cp "$NATIVE_LOG" "$missing_net_loopback"
sed -i '/^\[PASS\] net-loopback\r*$/d' "$missing_net_loopback"
if grep -Fq '[PASS] net-loopback' "$missing_net_loopback"; then
	fail "could not construct native network fixture"
fi
expect_reject "missing native network marker" \
	"$VERIFIER" native "$missing_net_loopback" "$NATIVE_MANIFEST"

missing_net_pselect="$selftest_tmp/native-missing-net-pselect.log"
cp "$NATIVE_LOG" "$missing_net_pselect"
sed -i '/^\[PASS\] net-pselect\r*$/d' "$missing_net_pselect"
if grep -Fq '[PASS] net-pselect' "$missing_net_pselect"; then
	fail "could not construct native pselect fixture"
fi
expect_reject "missing native pselect marker" \
	"$VERIFIER" native "$missing_net_pselect" "$NATIVE_MANIFEST"

missing_ocall="$selftest_tmp/native-missing-ocall.log"
cp "$NATIVE_LOG" "$missing_ocall"
sed -i '/^\[NATIVE CRYPTO\] PASS: uaccess faults, OCALL bounds, attestation nonce chain, legacy\/v1 sealing, tamper rejection, random uniqueness, and FP context isolation verified\r*$/d' \
	"$missing_ocall"
if grep -Fq '[NATIVE CRYPTO] PASS: uaccess faults, OCALL bounds' \
	"$missing_ocall"; then
	fail "could not construct native OCALL marker fixture"
fi
expect_reject "missing native crypto/FP marker" \
	"$VERIFIER" native "$missing_ocall" "$NATIVE_MANIFEST"

missing_protected_create="$selftest_tmp/native-missing-protected-create.log"
cp "$NATIVE_LOG" "$missing_protected_create"
sed -i '/\[NATIVE HANDLE\] PASS: protected SM and trusted-pool create ranges rejected/d' \
	"$missing_protected_create"
if grep -Fq '[NATIVE HANDLE] PASS: protected SM and trusted-pool create ranges rejected' \
	"$missing_protected_create"; then
	fail "could not construct native protected-create fixture"
fi
expect_reject "missing native protected-create marker" \
	"$VERIFIER" native "$missing_protected_create" "$NATIVE_MANIFEST"

missing_driver_security="$selftest_tmp/native-missing-driver-security.log"
cp "$NATIVE_LOG" "$missing_driver_security"
sed -i '/^\[DRIVER SECURITY\] PASS: all negative ABI tests\r*$/d' \
	"$missing_driver_security"
if grep -Fq '[DRIVER SECURITY] PASS: all negative ABI tests' \
	"$missing_driver_security"; then
	fail "could not construct native driver-security fixture"
fi
expect_reject "missing native driver-security marker" \
	"$VERIFIER" native "$missing_driver_security" "$NATIVE_MANIFEST"

fatal_log="$selftest_tmp/native-fatal.log"
cp "$NATIVE_LOG" "$fatal_log"
sed -i '1a[FAIL] injected verifier self-test failure' "$fatal_log"
expect_reject "fatal failure marker" \
	"$VERIFIER" native "$fatal_log" "$NATIVE_MANIFEST"

tampered_manifest="$selftest_tmp/replaced-manifest.sha256"
cp "$CVM_MANIFEST" "$tampered_manifest"
sed -i '$a# replaced manifest' "$tampered_manifest"
expect_reject "artifact manifest not bound to log" \
	"$VERIFIER" cvm "$CVM_LOG" "$tampered_manifest"

echo "[RUNTIME LOG SELFTEST] PASS: all positive and negative cases behaved as required"
