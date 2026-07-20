#!/usr/bin/env bash

set -euo pipefail

readonly ROOT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"

require_command()
{
	if ! command -v "$1" >/dev/null 2>&1; then
		echo "error: required command not found: $1" >&2
		exit 1
	fi
}

clone_or_update()
{
	local name="$1"
	local url="$2"
	local branch="$3"
	local dir="${ROOT_DIR}/${name}"

	if [ ! -e "$dir" ]; then
		echo "==> Cloning ${name} (${branch})"
		git clone --single-branch --branch "$branch" "$url" "$dir"
		return
	fi

	if [ ! -d "$dir/.git" ]; then
		echo "error: ${dir} exists but is not a git repository" >&2
		exit 1
	fi

	if ! git -C "$dir" diff --quiet ||
	   ! git -C "$dir" diff --cached --quiet; then
		echo "error: ${name} has tracked local changes; commit or stash them first" >&2
		exit 1
	fi

	echo "==> Updating ${name} (${branch})"
	if git -C "$dir" remote get-url origin >/dev/null 2>&1; then
		git -C "$dir" remote set-url origin "$url"
	else
		git -C "$dir" remote add origin "$url"
	fi

	# Keep future fetches restricted to the selected public branch.
	git -C "$dir" config --replace-all remote.origin.fetch \
		"+refs/heads/${branch}:refs/remotes/origin/${branch}"
	git -C "$dir" fetch --prune origin

	if git -C "$dir" show-ref --verify --quiet "refs/heads/${branch}"; then
		git -C "$dir" checkout "$branch"
	else
		git -C "$dir" checkout --track -b "$branch" "origin/${branch}"
	fi

	git -C "$dir" merge --ff-only "origin/${branch}"
}

require_command git

clone_or_update opensbi \
	https://github.com/sdww0/zion.git main
clone_or_update u-boot \
	https://github.com/sdww0/rockos-u-boot.git rockos-v2024.01
clone_or_update zion-host \
	https://github.com/sdww0/rockos-kernel.git zion-host
clone_or_update zion-guest \
	https://github.com/sdww0/rockos-kernel.git zion-guest
clone_or_update qemu \
	https://github.com/sdww0/qemu.git zion-qemu

echo "==> Zion source repositories are ready"
