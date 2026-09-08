//******************************************************************************
// Copyright (c) 2018, The Regents of the University of California (Regents).
// All Rights Reserved. See LICENSE for license details.
//------------------------------------------------------------------------------
#ifndef _ZION_SHARED_USER_H_
#define _ZION_SHARED_USER_H_

#include <linux/ioctl.h>
#include <linux/types.h>

#ifndef __KERNEL__
#include <stdint.h>
#endif

#include "sm_call.h"

// Linux generic TEE subsystem magic defined in <linux/tee.h>
#define ZION_IOC_MAGIC 0xa4

// ioctl definition
#define ZION_IOC_CREATE_ENCLAVE \
  _IOR(ZION_IOC_MAGIC, 0x00, struct zion_ioctl_create_enclave)
#define ZION_IOC_DESTROY_ENCLAVE \
  _IOW(ZION_IOC_MAGIC, 0x01, struct zion_ioctl_create_enclave)
#define ZION_IOC_RUN_ENCLAVE \
  _IOR(ZION_IOC_MAGIC, 0x04, struct zion_ioctl_run_enclave)
#define ZION_IOC_RESUME_ENCLAVE \
  _IOR(ZION_IOC_MAGIC, 0x05, struct zion_ioctl_run_enclave)
#define ZION_IOC_FINALIZE_ENCLAVE \
  _IOR(ZION_IOC_MAGIC, 0x06, struct zion_ioctl_create_enclave)
#define ZION_IOC_UTM_INIT \
  _IOR(ZION_IOC_MAGIC, 0x07, struct zion_ioctl_create_enclave)

#define RT_NOEXEC 0
#define USER_NOEXEC 1
#define RT_FULL 2
#define USER_FULL 3
#define UTM_FULL 4

struct zion_ioctl_create_enclave {
  uintptr_t eid;

  // host -> driver
  uintptr_t min_pages; // create
  uintptr_t utm_size; // utm_init

  // host -> driver // finalize
  uintptr_t runtime_paddr;
  uintptr_t user_paddr;
  uintptr_t free_paddr;
  uintptr_t free_requested;

  // driver -> host
  uintptr_t epm_paddr;
  uintptr_t epm_size;
  uintptr_t utm_paddr;
};

struct zion_ioctl_run_enclave {
  uintptr_t eid;
  uintptr_t error;
  uintptr_t value;
};

#endif
