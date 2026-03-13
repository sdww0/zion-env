#! /bin/bash


insmod ./tvm-driver.ko
./tvm-control tvm 200000 # 200000 * 4KB = 782 MB
./tee-sqlite.sh
