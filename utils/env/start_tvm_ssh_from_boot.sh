#! /bin/bash

set -e

ip link set eth0 up 2>/dev/null || true
ip addr add 10.0.2.15/24 dev eth0 2>/dev/null || true
ip route add default via 10.0.2.2 dev eth0 2>/dev/null || true

insmod ./tvm-driver.ko
./tvm-control tvm 200000 # 200000 * 4KB = 782 MB
./tee-ssh.sh
