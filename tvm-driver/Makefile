DRIVER = tvm-driver.ko

ifneq ($(KERNELRELEASE),)
	tvm-driver-y := \
		tvm.o \
		tvm-ioctl.o 
	obj-m += tvm-driver.o
else

PWD := $(shell pwd)
LINUXSRC := ../zion-host

tvm-control:
	riscv64-unknown-linux-gnu-gcc tvm-control.c -o tvm-control

tvm-driver:
	$(MAKE) -C $(LINUXSRC) ARCH=riscv CROSS_COMPILE=riscv64-unknown-linux-gnu- M=$(PWD) LOCALVERSION="-win2030"

endif

clean:
	rm -rvf *.o *.ko *.order *.symvers *.mod.c .tmp_versions .*o.cmd tvm-control
