DRIVER = zion-driver.ko

ifneq ($(KERNELRELEASE),)

ifeq ($(ZION_UNSAFE_DEBUG_IOCTLS),1)
	ccflags-y += -DZION_UNSAFE_DEBUG_IOCTLS
endif

ifeq ($(ZION_UNIFIED),1)
	zion-driver-y := \
		zion-main.o \
		zion-enclave-frontend.o \
		zion-enclave-page.o \
		zion-enclave-ioctl.o \
		zion-enclave-memory.o \
		zion-enclave-sbi.o \
		zion-tvm.o \
		zion-tvm-ioctl.o
	obj-m += zion-driver.o
else
	zion-driver-y := \
		zion-enclave-driver.o \
		zion-enclave-page.o \
		zion-enclave-ioctl.o \
		zion-enclave-memory.o \
		zion-enclave-sbi.o
	obj-m += zion-driver.o
endif

ifeq ($(ZION_SDK_DIR),)
    $(error ZION_SDK_DIR not defined)
endif

ccflags-y += -I$(ZION_SDK_DIR)/include/shared
else

PWD := $(shell pwd)
LINUXSRC := $(PWD)/../linux

default:
	$(MAKE) -C $(LINUXSRC) ARCH=riscv CROSS_COMPILE=riscv$(ZION_BITS)-unknown-linux-gnu- M=$(PWD)

endif

clean:
	rm -rvf *.o *.ko *.order *.symvers *.mod.c .tmp_versions .*o.cmd
