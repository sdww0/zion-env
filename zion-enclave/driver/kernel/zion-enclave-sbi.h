//******************************************************************************
// Copyright (c) 2018, The Regents of the University of California (Regents).
// All Rights Reserved. See LICENSE for license details.
//------------------------------------------------------------------------------
#ifndef _ZION_ENCLAVE_SBI_
#define _ZION_ENCLAVE_SBI_

#include "zion_user.h"
#include "sm_call.h"
#include "sm_err.h"

#include <asm/sbi.h>

/* Zion SBI function IDs (CVM range: 1000-1999) */
#define SBI_SM_RESERVE_MEM	1014

/* QEMU compatibility defaults. Hardware platforms should describe a no-map
 * zion,trusted-memory region in reserved-memory instead. */
#define SM_POOL_BASE_DEFAULT	0xF8000000UL
#define SM_POOL_SIZE_DEFAULT	(128 * 1024 * 1024UL)

struct sbiret sbi_sm_create_enclave(struct zion_sbi_create_t* args);
struct sbiret sbi_sm_destroy_enclave(unsigned long eid);
struct sbiret sbi_sm_run_enclave(unsigned long eid);
struct sbiret sbi_sm_resume_enclave(unsigned long eid);
struct sbiret sbi_sm_reserve_mem(unsigned long base, unsigned long pages);

#endif
