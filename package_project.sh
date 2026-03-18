#! /bin/bash

rm all_commits.patch
rm zion.zip
# Format patches for all commits in the current branch
commit_count=$(git rev-list --count HEAD)
git format-patch -$commit_count --stdout > all_commits.patch

zip -r zion.zip ./patch ./virt/initrd.img-6.6.87-win2030 ./utils/env/initrd.img ./all_commits.patch ./utils/docker/ubuntu-24.04-amd64.tar ./utils/docker/ubuntu-24.04-riscv.tar init.sh
