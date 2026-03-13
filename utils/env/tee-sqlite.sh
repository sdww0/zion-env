#! /bin/bash

/usr/local/qemu/bin/qemu-system-riscv64 -m 512M \
    --enable-kvm \
    -cpu rv64 \
    -nographic \
    -machine virt \
    -kernel ./guest_kernel_image \
    -initrd initrd.img \
    -append "console=ttyS0 ostd.log_level=error loglevel=7 memmap=2M\$0x80000000 rdinit=/etc/sqlite.sh init=/usr/bin/busybox -- sh /etc/sqlite.sh"
