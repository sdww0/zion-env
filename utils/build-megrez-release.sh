#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
if [[ ${1:-} == --help || $# -lt 2 || $# -gt 3 ]]; then
	echo "Usage: bash $0 VALIDATED_RUNTIME_RELEASE UBOOT_BASE_RELEASE [NEW_RELEASE_DIRECTORY]"
	echo 'Rebuild OpenSBI, sign firmware, and package the validated kernels/runtime.'
	[[ ${1:-} == --help ]] && exit 0
	exit 2
fi
export MEGREZ_RUNTIME_INPUT=$(realpath "$1")
export MEGREZ_BASE_RELEASE=$(realpath "$2")
destination=${3:-$ROOT/output/zion-megrez-release-$(date +%Y%m%d-%H%M%S)}
[[ ! -e $destination ]] || { echo "Destination exists: $destination" >&2; exit 1; }
[[ -f $MEGREZ_RUNTIME_INPUT/SHA256SUMS ]] || exit 1
(cd "$MEGREZ_RUNTIME_INPUT" && sha256sum -c SHA256SUMS)
export MEGREZ_FIRMWARE_BUILD=$ROOT/output/megrez-firmware-$(date +%Y%m%d-%H%M%S)
bash "$ROOT/utils/build-megrez-release-firmware.sh" "$MEGREZ_FIRMWARE_BUILD"
bash "$ROOT/utils/package-megrez-runtime.sh" "$destination"
