#!/bin/sh

set -eu

artifact_dir="${ZION_ENCLAVE_ARTIFACT_DIR:-/root/zion-enclave}"

if [ ! -e /dev/zion_enclave ]; then
	insmod "$artifact_dir/zion-driver.ko" nested_cvm=1
fi

exec "$artifact_dir/hello-runner" \
	"$artifact_dir/hello" \
	"$artifact_dir/eyrie-rt" \
	"$artifact_dir/loader.bin"
