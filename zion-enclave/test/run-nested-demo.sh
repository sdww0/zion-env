#!/bin/sh
set -eu

artifacts="${ZION_ENCLAVE_ARTIFACT_DIR:-/root/zion-enclave}"
if [ ! -e /dev/zion_enclave ]; then
    insmod "$artifacts/zion-driver.ko" nested_cvm=1
fi
echo '[ZION DEMO] Run inside CVM Guest Linux, not the outer Host'
"$artifacts/demo-runner" "$artifacts/demo" \
    "$artifacts/eyrie-rt" "$artifacts/loader.bin"
"$artifacts/zion-driver-security"
echo '[ZION DEMO] PASS: compute lifecycle and driver negative ABI tests'
echo '[ZION DEMO] SKIP: arbitrary private-page access; dedicated safe probe required'
