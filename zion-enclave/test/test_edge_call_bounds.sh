#!/usr/bin/env bash
# Build and run the edge-call boundary test with host sanitizers when available.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ZION_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ZION_DIR/build}"
HOST_CC="${HOST_CC:-${CC:-cc}}"

fail() {
	echo "[FAIL] $*" >&2
	exit 1
}

command -v "$HOST_CC" >/dev/null || fail "missing host C compiler: $HOST_CC"
mkdir -p "$BUILD_DIR"
selftest_tmp="$(mktemp -d "$BUILD_DIR/edge-call-selftest.XXXXXX")"
cleanup() {
	case "$selftest_tmp" in
		"$BUILD_DIR"/edge-call-selftest.*) find "$selftest_tmp" -depth -delete ;;
		*) echo "[WARN] refusing to remove unexpected path: $selftest_tmp" >&2 ;;
	esac
}
trap cleanup EXIT

common_flags=(
	-std=c11
	-Wall
	-Wextra
	-Werror
	-DIO_SYSCALL_WRAPPING
	-I "$ZION_DIR/sdk/include/edge"
	"$ZION_DIR/test/edge-call-bounds.c"
	"$ZION_DIR/sdk/src/edge/edge_call.c"
	"$ZION_DIR/sdk/src/edge/edge_dispatch.c"
	"$ZION_DIR/sdk/src/edge/edge_syscall.c"
)

"$HOST_CC" "${common_flags[@]}" -o "$selftest_tmp/edge-call-bounds"
test_binary="$selftest_tmp/edge-call-bounds"
sanitizer_status="not available"
if "$HOST_CC" "${common_flags[@]}" \
		-fsanitize=address,undefined -fno-omit-frame-pointer \
		-o "$selftest_tmp/edge-call-bounds-sanitized" >/dev/null 2>&1; then
	test_binary="$selftest_tmp/edge-call-bounds-sanitized"
	sanitizer_status="enabled"
fi

echo "[EDGE CALL SELFTEST] sanitizers: $sanitizer_status"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
	UBSAN_OPTIONS=halt_on_error=1 "$test_binary"
