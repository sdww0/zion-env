#!/usr/bin/env bash
# Exercise Zion's path precedence and failure behavior without using host
# artifacts. This keeps machine-specific paths from creeping back into the
# supported entry points.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ZION_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
source "$ZION_DIR/scripts/zion_paths.sh"

fail() {
	echo "[FAIL] $*" >&2
	exit 1
}

selftest_tmp="$(mktemp -d "${TMPDIR:-/tmp}/zion-path-selftest.XXXXXX")"
cleanup() {
	case "$selftest_tmp" in
		"${TMPDIR:-/tmp}"/zion-path-selftest.*)
			find "$selftest_tmp" -depth -delete
			;;
		*) echo "[WARN] refusing to remove unexpected path: $selftest_tmp" >&2 ;;
	esac
}
trap cleanup EXIT

fake_zion="$selftest_tmp/zion"
fake_build="$selftest_tmp/output"
mkdir -p "$fake_zion" "$selftest_tmp/build/qemu" \
	"$fake_build/native-inputs/linux/arch/riscv/boot" \
	"$fake_build/native-inputs/linux/include/config" \
	"$fake_build/native-inputs/busybox" \
	"$fake_build/native-linux/arch/riscv/boot" \
	"$fake_build/native-linux/include/config" \
	"$fake_build/busybox" "$selftest_tmp/bin"

for executable in \
	"$selftest_tmp/build/qemu/qemu-system-riscv64" \
	"$selftest_tmp/bin/qemu-system-riscv64"; do
	: > "$executable"
	chmod +x "$executable"
done
: > "$fake_build/native-linux/arch/riscv/boot/Image"
: > "$fake_build/native-linux/include/config/auto.conf"
: > "$fake_build/busybox/rootfs.cpio"
: > "$fake_build/native-inputs/linux/arch/riscv/boot/Image"
: > "$fake_build/native-inputs/linux/include/config/auto.conf"
: > "$fake_build/native-inputs/busybox/rootfs.cpio"

[ "$(zion_resolve_outer_qemu "$fake_zion")" = \
	"$fake_zion/../build/qemu/qemu-system-riscv64" ] ||
	fail "repository-relative outer QEMU did not win"
[ "$(zion_resolve_native_kernel "$fake_zion" "$fake_build")" = \
	"$fake_build/native-inputs/linux/arch/riscv/boot/Image" ] ||
	fail "reproducible native kernel did not win"
[ "$(zion_resolve_native_linux_source "$fake_zion" "$fake_build")" = \
	"$fake_build/native-inputs/linux" ] ||
	fail "reproducible native Linux source did not win"
[ "$(zion_resolve_zion_base_rootfs "$fake_zion" "$fake_build")" = \
	"$fake_build/native-inputs/busybox/rootfs.cpio" ] ||
	fail "reproducible base initramfs did not win"

# Older build-local layouts remain fallbacks when reproducible inputs have not
# been built yet.
find "$fake_build/native-inputs" -type f -delete
[ "$(zion_resolve_native_kernel "$fake_zion" "$fake_build")" = \
	"$fake_build/native-linux/arch/riscv/boot/Image" ] ||
	fail "legacy build-local native kernel fallback failed"
[ "$(zion_resolve_native_linux_source "$fake_zion" "$fake_build")" = \
	"$fake_build/native-linux" ] ||
	fail "legacy configured native Linux fallback failed"
[ "$(zion_resolve_zion_base_rootfs "$fake_zion" "$fake_build")" = \
	"$fake_build/busybox/rootfs.cpio" ] ||
	fail "legacy build-local initramfs fallback failed"

if QEMU="$selftest_tmp/missing-qemu" \
	zion_resolve_outer_qemu "$fake_zion" >/dev/null 2>&1; then
	fail "invalid explicit QEMU override was accepted"
fi
[ "$(QEMU="$selftest_tmp/bin/qemu-system-riscv64" \
	zion_resolve_outer_qemu "$fake_zion")" = \
	"$selftest_tmp/bin/qemu-system-riscv64" ] ||
	fail "explicit QEMU override did not win"

path_only_zion="$selftest_tmp/isolated/path-only-zion"
mkdir -p "$path_only_zion"
[ "$(PATH="$selftest_tmp/bin:/usr/bin:/bin" \
	zion_resolve_outer_qemu "$path_only_zion")" = \
	"$selftest_tmp/bin/qemu-system-riscv64" ] ||
	fail "PATH QEMU fallback failed"

custom_build="$selftest_tmp/custom-build"
BUILD_DIR="$custom_build" bash -c '
	source "$1"
	[ "$BUILD_DIR" = "$2" ]
' _ "$ZION_DIR/scripts/config.sh" "$custom_build" ||
	fail "config.sh discarded BUILD_DIR override"

set +e
"$ZION_DIR/scripts/doctor.sh" invalid >/dev/null 2>&1
doctor_status=$?
set -e
[ "$doctor_status" -eq 2 ] ||
	fail "doctor invalid mode returned $doctor_status instead of 2"

echo "[PATH SELFTEST] PASS: overrides, relative discovery, PATH fallback, and failures verified"
