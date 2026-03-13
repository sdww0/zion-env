#! /bin/bash

pushd opensbi
git apply ../patch/opensbi/*.patch
popd

pushd qemu
git apply ../patch/qemu/*.patch
popd

pushd zion-host
git apply ../patch/zion-host/*.patch
popd

