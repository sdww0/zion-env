#! /bin/bash

git clone https://github.com/sdww0/rockos-opensbi.git opensbi
git clone https://github.com/sdww0/rockos-u-boot.git u-boot
git clone https://github.com/sdww0/rockos-kernel.git zion-host
cp -r zion-host zion-guest
git clone --single-branch -b stable-10.1 https://github.com/sdww0/qemu.git qemu
