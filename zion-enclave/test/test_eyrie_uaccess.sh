#!/usr/bin/env bash
# Verify that the linked Eyrie image retains the fault-recovery metadata used
# by copy_{to,from}_user. The native QEMU regression exercises these entries;
# this check prevents a linker or build-layout regression from silently
# dropping them before QEMU starts.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ZION_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ZION_DIR/build}"
source "$ZION_DIR/scripts/zion_paths.sh"
CROSS_COMPILE="${CROSS_COMPILE:-$(zion_resolve_cross_compile)}"
EYRIE_RT="${1:-$BUILD_DIR/eyrie/eyrie-rt}"

fail() {
	echo "[FAIL] $*" >&2
	exit 1
}

for command_name in awk grep "${CROSS_COMPILE}nm" \
	"${CROSS_COMPILE}objdump"; do
	command -v "$command_name" >/dev/null ||
		fail "missing command: $command_name"
done
[ -s "$EYRIE_RT" ] || fail "missing Eyrie runtime: $EYRIE_RT"

mapfile -t exception_symbols < <(
	"${CROSS_COMPILE}nm" -n "$EYRIE_RT" |
		awk '$3 == "__ex_table_start" || $3 == "__ex_table_end" {
			print $1, $3
		}'
)
[ "${#exception_symbols[@]}" -eq 2 ] ||
	fail "Eyrie runtime does not expose both exception-table boundaries"

read -r start_addr start_name <<< "${exception_symbols[0]}"
read -r end_addr end_name <<< "${exception_symbols[1]}"
[ "$start_name" = __ex_table_start ] && [ "$end_name" = __ex_table_end ] ||
	fail "Eyrie exception-table boundaries are not ordered"
[[ "$start_addr" < "$end_addr" ]] ||
	fail "Eyrie exception table is empty or reversed"

symbols="$("${CROSS_COMPILE}nm" -n "$EYRIE_RT")"
for symbol in rt_uaccess_fixup __asm_copy_from_user __asm_copy_to_user; do
	grep -Eq "[[:space:]]${symbol}$" <<< "$symbols" ||
		fail "Eyrie runtime is missing uaccess symbol: $symbol"
done

disassembly="$("${CROSS_COMPILE}objdump" -d "$EYRIE_RT")"
grep -Eq '[[:space:]]csrc[[:space:]]+sstatus,t6' <<< "$disassembly" ||
	fail "uaccess return paths do not clear supervisor user-memory access"
grep -Eq '[[:space:]]sub[[:space:]]+a0,a3,a0' <<< "$disassembly" ||
	fail "uaccess fault paths do not return the uncopied byte count"
overflow_checks="$(
	grep -Ec '[[:space:]]bltu[[:space:]]+a3,a0,' <<< "$disassembly" || true
)"
[ "$overflow_checks" -ge 2 ] ||
	fail "uaccess copy/clear paths do not reject wrapped target ranges"

echo "[EYRIE UACCESS SELFTEST] PASS: linked fixup table and recovery paths verified"
