#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ZION_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ZION_DIR/build}"
CC_BIN="${CC:-cc}"
TEST_BIN="$BUILD_DIR/eyrie-elf-layout-selftest"

fail() {
	echo "[FAIL] $*" >&2
	exit 1
}

command -v "$CC_BIN" >/dev/null || fail "missing host C compiler: $CC_BIN"
mkdir -p "$BUILD_DIR"

grep -Fq 'eyrie_elf_user_layout(&elf_file, &initial_program_break)' \
	"$ZION_DIR/runtime/sys/boot.c" || \
	fail "boot path does not derive program break from the validated ELF layout"
grep -Fq 'set_program_break(initial_program_break)' \
	"$ZION_DIR/runtime/sys/boot.c" || \
	fail "boot path does not install the ELF-derived program break"
if grep -Fq 'set_program_break(EYRIE_ANON_REGION_START' \
		"$ZION_DIR/runtime/sys/boot.c"; then
	fail "boot path still contains the legacy hard-coded program break"
fi

common_flags=(
	-std=c11 -Wall -Wextra -Werror -D__riscv_xlen=64
	-I"$ZION_DIR/runtime/include"
)
sanitize_flags=(-fsanitize=address,undefined -fno-omit-frame-pointer)

if "$CC_BIN" "${common_flags[@]}" "${sanitize_flags[@]}" \
		-o "$TEST_BIN" \
		"$SCRIPT_DIR/eyrie-elf-layout.c" \
		"$ZION_DIR/runtime/loader/layout.c" \
		"$ZION_DIR/runtime/loader/elf.c" \
		"$ZION_DIR/runtime/loader/elf32.c" \
		"$ZION_DIR/runtime/loader/elf64.c"; then
	echo "[EYRIE ELF SELFTEST] sanitizers: enabled"
else
	"$CC_BIN" "${common_flags[@]}" -o "$TEST_BIN" \
		"$SCRIPT_DIR/eyrie-elf-layout.c" \
		"$ZION_DIR/runtime/loader/layout.c" \
		"$ZION_DIR/runtime/loader/elf.c" \
		"$ZION_DIR/runtime/loader/elf32.c" \
		"$ZION_DIR/runtime/loader/elf64.c"
	echo "[EYRIE ELF SELFTEST] sanitizers: unavailable"
fi

"$TEST_BIN"
