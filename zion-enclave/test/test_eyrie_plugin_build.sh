#!/usr/bin/env bash
# Compile Eyrie's optional Linux+IO+NET syscall producers against the hardened edge
# parser without mutating the default runtime artifacts in the source tree.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ZION_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ZION_DIR/build}"
SDK_INSTALL_DIR="${SDK_INSTALL_DIR:-$BUILD_DIR/sdk-install}"
source "$ZION_DIR/scripts/zion_paths.sh"
CROSS_COMPILE="${CROSS_COMPILE:-$(zion_resolve_cross_compile)}"

fail() {
	echo "[FAIL] $*" >&2
	exit 1
}

for command_name in cmake cmp sha256sum "${CROSS_COMPILE}gcc" \
	"${CROSS_COMPILE}objcopy"; do
	command -v "$command_name" >/dev/null || fail "missing command: $command_name"
done
[ -f "$SDK_INSTALL_DIR/lib/libzion-edge.a" ] ||
	fail "missing installed edge library: $SDK_INSTALL_DIR"
for header in eyrie_call.h sm_call.h; do
	cmp "$ZION_DIR/runtime/include/$header" \
		"$ZION_DIR/sdk/include/shared/$header" >/dev/null ||
		fail "runtime and SDK shared ABI headers differ: $header"
	cmp "$ZION_DIR/sdk/include/shared/$header" \
		"$SDK_INSTALL_DIR/include/shared/$header" >/dev/null ||
		fail "installed SDK shared ABI header is stale: $header"
done

mkdir -p "$BUILD_DIR"
matrix_tmp="$(mktemp -d "$BUILD_DIR/eyrie-plugin-selftest.XXXXXX")"
cleanup() {
	case "$matrix_tmp" in
		"$BUILD_DIR"/eyrie-plugin-selftest.*) find "$matrix_tmp" -depth -delete ;;
		*) echo "[WARN] refusing to remove unexpected path: $matrix_tmp" >&2 ;;
	esac
}
trap cleanup EXIT

source_outputs=(
	"$ZION_DIR/runtime/eyrie-rt"
	"$ZION_DIR/runtime/loader.bin"
	"$ZION_DIR/runtime/loader.elf"
	"$ZION_DIR/runtime/.options_log"
)
for output in "${source_outputs[@]}"; do
	[ -f "$output" ] || fail "missing default runtime output: $output"
done
sha256sum "${source_outputs[@]}" > "$matrix_tmp/source-before.sha256"

mkdir -p "$matrix_tmp/output"
cross_gcc="$(command -v "${CROSS_COMPILE}gcc")"
cross_objcopy="$(command -v "${CROSS_COMPILE}objcopy")"
cmake -S "$ZION_DIR/runtime" -B "$matrix_tmp/build" \
	-DZION_SDK_DIR="$SDK_INSTALL_DIR" \
	-DCMAKE_C_COMPILER="$cross_gcc" \
	-DCMAKE_ASM_COMPILER="$cross_gcc" \
	-DCMAKE_OBJCOPY="$cross_objcopy" \
	-DCMAKE_SYSTEM_NAME=Linux \
	-DCMAKE_SYSTEM_PROCESSOR=riscv64 \
	-DEYRIE_SRCDIR="$ZION_DIR/runtime" \
	-DEYRIE_OUTPUT_DIR="$matrix_tmp/output" \
	-DLINUX_SYSCALL=ON -DIO_SYSCALL=ON -DNET_SYSCALL=ON >/dev/null
cmake --build "$matrix_tmp/build" -j"${JOBS:-$(nproc)}"

for output in eyrie-rt loader.bin loader.elf .options_log; do
	[ -s "$matrix_tmp/output/$output" ] ||
		fail "plugin matrix did not produce: $output"
done
grep -Eq '(^| )IO_SYSCALL( |$)' "$matrix_tmp/output/.options_log" ||
	fail "IO_SYSCALL missing from plugin matrix options"
grep -Eq '(^| )NET_SYSCALL( |$)' "$matrix_tmp/output/.options_log" ||
	fail "NET_SYSCALL missing from plugin matrix options"
grep -Eq '(^| )LINUX_SYSCALL( |$)' "$matrix_tmp/output/.options_log" ||
	fail "LINUX_SYSCALL missing from plugin matrix options"
sha256sum -c "$matrix_tmp/source-before.sha256" >/dev/null ||
	fail "plugin matrix mutated default runtime outputs"
"$ZION_DIR/test/test_eyrie_uaccess.sh" "$matrix_tmp/output/eyrie-rt"

echo "[EYRIE PLUGIN SELFTEST] PASS: shared ABI, Linux+IO+NET build, and output isolation verified"
