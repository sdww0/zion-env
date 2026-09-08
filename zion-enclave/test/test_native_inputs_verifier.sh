#!/usr/bin/env bash
# Prove that the native-input gate accepts the reviewed artifacts and rejects
# artifact corruption, an unreviewed baseline, and self-consistent unpinned
# source metadata.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ZION_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ZION_DIR/build}"
VERIFIER="$ZION_DIR/scripts/verify_native_inputs.sh"
INPUT_DIR="${NATIVE_INPUT_DIR:-$BUILD_DIR/native-inputs}"
EXPECTED_MANIFEST="$ZION_DIR/deps/native/native-inputs.sha256"

fail() {
	echo "[FAIL] $*" >&2
	exit 1
}

for command_name in awk cp dirname env find ln mkdir mktemp rm sed sha256sum; do
	command -v "$command_name" >/dev/null ||
		fail "missing command: $command_name"
done
[ -x "$VERIFIER" ] || fail "missing native-input verifier: $VERIFIER"
[ -d "$INPUT_DIR" ] || fail "missing native-input fixture: $INPUT_DIR"
[ -f "$EXPECTED_MANIFEST" ] ||
	fail "missing reviewed native-input manifest: $EXPECTED_MANIFEST"
INPUT_DIR="$(cd "$INPUT_DIR" && pwd)"

mkdir -p "$BUILD_DIR"
selftest_tmp="$(mktemp -d "$BUILD_DIR/native-input-selftest.XXXXXX")"
cleanup() {
	case "$selftest_tmp" in
		"$BUILD_DIR"/native-input-selftest.*)
			find "$selftest_tmp" -depth -delete
			;;
		*) echo "[WARN] refusing to remove unexpected path: $selftest_tmp" >&2 ;;
	esac
}
trap cleanup EXIT

make_fixture() {
	local destination="$1" digest path
	mkdir -p "$destination"
	cp "$INPUT_DIR/MANIFEST.sha256" "$destination/MANIFEST.sha256"
	while read -r digest path; do
		[ -n "$digest" ] && [ -n "$path" ] ||
			fail "invalid native-input manifest entry"
		mkdir -p "$destination/$(dirname "$path")"
		ln "$INPUT_DIR/$path" "$destination/$path"
	done < "$INPUT_DIR/MANIFEST.sha256"
}

expect_reject() {
	local label="$1"
	shift
	if "$@" >/dev/null 2>&1; then
		fail "native-input verifier accepted negative case: $label"
	fi
	echo "[NATIVE INPUT SELFTEST] PASS: $label rejected"
}

positive="$selftest_tmp/positive"
make_fixture "$positive"
"$VERIFIER" "$positive" >/dev/null
echo "[NATIVE INPUT SELFTEST] PASS: reviewed artifacts accepted"

corrupted="$selftest_tmp/corrupted"
make_fixture "$corrupted"
rm "$corrupted/source-manifest.txt"
cp "$INPUT_DIR/source-manifest.txt" "$corrupted/source-manifest.txt"
printf 'injected_corruption=1\n' >> "$corrupted/source-manifest.txt"
expect_reject "artifact corruption" "$VERIFIER" "$corrupted"

unreviewed="$selftest_tmp/unreviewed"
make_fixture "$unreviewed"
rm "$unreviewed/source-manifest.txt"
cp "$INPUT_DIR/source-manifest.txt" "$unreviewed/source-manifest.txt"
printf 'review_status=unreviewed\n' >> "$unreviewed/source-manifest.txt"
unreviewed_hash="$(sha256sum "$unreviewed/source-manifest.txt" | awk '{print $1}')"
sed -i \
	"s/^[0-9a-f]\{64\}  source-manifest.txt$/$unreviewed_hash  source-manifest.txt/" \
	"$unreviewed/MANIFEST.sha256"
expect_reject "self-consistent unreviewed manifest" \
	"$VERIFIER" "$unreviewed"

unpinned="$selftest_tmp/unpinned"
make_fixture "$unpinned"
rm "$unpinned/source-manifest.txt"
cp "$INPUT_DIR/source-manifest.txt" "$unpinned/source-manifest.txt"
sed -i \
	's/^linux_commit=.*/linux_commit=0000000000000000000000000000000000000000/' \
	"$unpinned/source-manifest.txt"
unpinned_hash="$(sha256sum "$unpinned/source-manifest.txt" | awk '{print $1}')"
sed -i \
	"s/^[0-9a-f]\{64\}  source-manifest.txt$/$unpinned_hash  source-manifest.txt/" \
	"$unpinned/MANIFEST.sha256"
unpinned_expected="$selftest_tmp/unpinned-expected.sha256"
cp "$unpinned/MANIFEST.sha256" "$unpinned_expected"
expect_reject "unpinned metadata with attempted trust-root override" env \
	NATIVE_INPUTS_EXPECTED_MANIFEST="$unpinned_expected" \
	"$VERIFIER" "$unpinned"

echo "[NATIVE INPUT SELFTEST] PASS: all positive and negative cases behaved as required"
