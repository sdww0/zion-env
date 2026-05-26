#! /bin/bash

set -e

TVM_PAGE_COUNT="${TVM_PAGE_COUNT:-200000}" # 200000 * 4KB = 782 MB

insmod ./tvm-driver.ko
./tvm-control tvm "$TVM_PAGE_COUNT"
./tee-asterinas.sh
