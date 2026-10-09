#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
case ${1:---dry-run} in
	--dry-run|--apply) mode=${1:---dry-run} ;;
	--help) echo "Usage: bash $0 [--dry-run|--apply]"; exit 0 ;;
	*) echo 'Use --dry-run or --apply.' >&2; exit 2 ;;
esac
[[ $# -le 1 ]] || exit 2
# Keep toolchains, downloaded dependencies, runtime images, releases and logs.
paths=(utils/__pycache__ output/opensbi-check-default output/opensbi-check-dynamic
	output/zion-enclave-commit-check
	$'output/asterinas-build-\n  cache')
for path in "${paths[@]}"; do
	target=$ROOT/$path
	[[ -e $target ]] || continue
	[[ ! -L $target ]] || { echo "Skip symlink: $target"; continue; }
	if mountpoint -q "$target" || findmnt -rn -o TARGET | awk -v p="$target/" \
		'index($0,p)==1 {found=1} END {exit !found}'; then
		echo "Skip mounted tree: $target" >&2
		continue
	fi
	du -sh -- "$target"
	if [[ $mode == --apply ]]; then rm -rf --one-file-system -- "$target"; fi
done
echo "Cache cleanup: $mode"
