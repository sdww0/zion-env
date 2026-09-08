// SPDX-License-Identifier: Dual BSD/GPL
/* Native REE regression for stale and non-canonical enclave handles. */

#include <asm/sbi.h>
#include <linux/errno.h>
#include <linux/gfp.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/module.h>

#define SBI_EXT_EXPERIMENTAL_ZION_ENCLAVE 0x08424b45

#define SBI_SM_CREATE_ENCLAVE 2001
#define SBI_SM_DESTROY_ENCLAVE 2002
#define SBI_SM_RUN_ENCLAVE 2003
#define SBI_SM_RESUME_ENCLAVE 2005

#define SBI_ERR_SM_ENCLAVE_NOT_RUNNABLE 100004
#define SBI_ERR_SM_ENCLAVE_NOT_DESTROYABLE 100005
#define SBI_ERR_SM_ENCLAVE_ILLEGAL_ARGUMENT 100008

#define ZION_SMM_BASE 0x80000000UL
#define ZION_SMM_SIZE 0x200000UL
#define ZION_NATIVE_CORE_BASE 0xf8000000UL

struct zion_sbi_pregion {
	unsigned long paddr;
	unsigned long size;
};

struct zion_sbi_create {
	struct zion_sbi_pregion epm_region;
	struct zion_sbi_pregion utm_region;
	unsigned long runtime_paddr;
	unsigned long user_paddr;
	unsigned long free_paddr;
	unsigned long free_requested;
};

static void set_create_layout(struct zion_sbi_create *args,
			      unsigned long epm, unsigned long epm_size)
{
	memset(args, 0, sizeof(*args));
	args->epm_region.paddr = epm;
	args->epm_region.size = epm_size;
	args->runtime_paddr = epm;
	args->user_paddr = epm + PAGE_SIZE;
	args->free_paddr = epm + 2 * PAGE_SIZE;
	args->free_requested = PAGE_SIZE;
}

static int expect_create_rejection(struct zion_sbi_create *args,
				   const char *kind)
{
	struct sbiret ret;

	ret = sbi_ecall(SBI_EXT_EXPERIMENTAL_ZION_ENCLAVE,
			SBI_SM_CREATE_ENCLAVE, (unsigned long)args,
			0, 0, 0, 0, 0);
	if (ret.error != SBI_ERR_SM_ENCLAVE_ILLEGAL_ARGUMENT) {
		pr_err("[NATIVE HANDLE] FAIL: protected %s create returned %ld/%lx, expected %d\n",
		       kind, ret.error, ret.value,
		       SBI_ERR_SM_ENCLAVE_ILLEGAL_ARGUMENT);
		return -EINVAL;
	}
	return 0;
}

static int reject_protected_create_ranges(void)
{
	struct zion_sbi_create args;
	struct page *epm;
	unsigned long epm_pa;
	int ret = -EINVAL;

	set_create_layout(&args, ZION_SMM_BASE, ZION_SMM_SIZE);
	if (expect_create_rejection(&args, "SM EPM"))
		return -EINVAL;

	set_create_layout(&args, ZION_NATIVE_CORE_BASE, ZION_SMM_SIZE);
	if (expect_create_rejection(&args, "trusted-pool EPM"))
		return -EINVAL;

	epm = alloc_pages(GFP_KERNEL | __GFP_ZERO, 2);
	if (!epm)
		return -ENOMEM;
	epm_pa = page_to_phys(epm);
	set_create_layout(&args, epm_pa, PAGE_SIZE << 2);
	args.utm_region.paddr = ZION_SMM_BASE;
	args.utm_region.size = PAGE_SIZE;
	if (!expect_create_rejection(&args, "SM UTM"))
		ret = 0;
	__free_pages(epm, 2);
	return ret;
}

static int expect_rejection(const char *operation, unsigned long function_id,
			    unsigned long handle, long expected)
{
	struct sbiret ret;

	ret = sbi_ecall(SBI_EXT_EXPERIMENTAL_ZION_ENCLAVE, function_id,
			handle, 0, 0, 0, 0, 0);
	if (ret.error != expected) {
		pr_err("[NATIVE HANDLE] FAIL: %s handle=0x%lx returned %ld, expected %ld\n",
		       operation, handle, ret.error, expected);
		return -EINVAL;
	}

	return 0;
}

static int reject_handle(unsigned long handle, const char *kind)
{
	if (expect_rejection("run", SBI_SM_RUN_ENCLAVE, handle,
			     SBI_ERR_SM_ENCLAVE_NOT_RUNNABLE) ||
	    expect_rejection("resume", SBI_SM_RESUME_ENCLAVE, handle,
			     SBI_ERR_SM_ENCLAVE_NOT_RUNNABLE) ||
	    expect_rejection("destroy", SBI_SM_DESTROY_ENCLAVE, handle,
			     SBI_ERR_SM_ENCLAVE_NOT_DESTROYABLE)) {
		pr_err("[NATIVE HANDLE] FAIL: %s handle rejection incomplete\n",
		       kind);
		return -EINVAL;
	}

	return 0;
}

static int __init native_enclave_handles_init(void)
{
	if (reject_protected_create_ranges())
		return -EINVAL;
	pr_info("[NATIVE HANDLE] PASS: protected SM and trusted-pool create ranges rejected\n");

	/* The QEMU harness boots a fresh SM, creates handle 0, destroys it, and
	 * reuses slot 0 several times before loading this module. */
	if (reject_handle(0, "stale"))
		return -EINVAL;

#if BITS_PER_LONG > 32
	if (reject_handle(1UL << 32, "non-canonical"))
		return -EINVAL;
#endif

	pr_info("[NATIVE HANDLE] PASS: stale and noncanonical run/resume/destroy rejected after slot reuse\n");
	return 0;
}

static void __exit native_enclave_handles_exit(void)
{
}

module_init(native_enclave_handles_init);
module_exit(native_enclave_handles_exit);

MODULE_DESCRIPTION("Zion native enclave protected-range and generation-handle regression");
MODULE_AUTHOR("Zion project");
MODULE_LICENSE("Dual BSD/GPL");
