// SPDX-License-Identifier: GPL-2.0-only
/* REE-side direct-SBI regression for CVM management address validation. */

#include <asm/sbi.h>
#include <linux/fs.h>
#include <linux/gfp.h>
#include <linux/init.h>
#include <linux/ioctl.h>
#include <linux/miscdevice.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/uaccess.h>

#define ZION_SBI_EXT 0x08424b45
#define SBI_SM_CREATE_CVM 1015
#define SBI_SM_REGISTER_PT 1021
#define SBI_SM_DESTROY_CVM 1023
#define SBI_SM_LOAD_MEM 1025
#define SBI_SM_SYNC_PT 1034
#define SBI_SM_QUERY_MEM_EXTENT 1037
#define ZION_SMM_BASE 0x80000000UL
#define CVM_MANAGEMENT_RUN _IO('Z', 1)
#define CVM_MANAGEMENT_CAPACITY _IOW('Z', 2, unsigned long)
#define CVM_MANAGEMENT_MAX_CAPACITY 16

struct zion_register_pt {
	unsigned long gpa;
	unsigned long hfn;
	unsigned long level;
	unsigned int is_huge;
	unsigned int rdonly;
};

struct zion_load_mem {
	unsigned long stash;
	unsigned long pos;
	unsigned long size;
};

struct zion_extent_info {
	u64 base;
	u64 size;
	u64 data_base;
	u64 data_size;
	u32 id;
	u32 generation;
	u32 state;
	u32 total_blocks;
	u32 free_blocks;
	s32 pmp_region_id;
};

static int expect_failure(const char *name, struct sbiret ret)
{
	if (ret.error)
		return 0;
	pr_err("[CVM MANAGEMENT] FAIL: %s unexpectedly succeeded (value=%lx)\n",
	       name, ret.value);
	return -EINVAL;
}

static long cvm_management_run(unsigned long stash)
{
	struct zion_extent_info core;
	struct zion_register_pt pt = { 0 };
	struct zion_load_mem load = { 0 };
	struct page *page = NULL;
	struct sbiret ret;
	unsigned long tid = 0;
	u8 probe[2];
	int result = -EINVAL;

	if (copy_from_user(probe, (void __user *)stash, sizeof(probe)))
		return -EFAULT;
	ret = sbi_ecall(ZION_SBI_EXT, SBI_SM_QUERY_MEM_EXTENT, 0,
			(unsigned long)&core, 0, 0, 0, 0);
	if (ret.error) {
		pr_err("[CVM MANAGEMENT] FAIL: core extent query returned %ld\n",
		       ret.error);
		return -EIO;
	}
	page = alloc_page(GFP_KERNEL | __GFP_ZERO);
	if (!page)
		return -ENOMEM;
	ret = sbi_ecall(ZION_SBI_EXT, SBI_SM_CREATE_CVM, 0, 0, 0, 0, 0, 0);
	if (ret.error) {
		pr_err("[CVM MANAGEMENT] FAIL: scratch CVM create returned %ld\n",
		       ret.error);
		goto out;
	}
	tid = ret.value;

	/* Ordinary REE pages remain the compatibility path used by host KVM. */
	pt.gpa = 0;
	pt.hfn = page_to_phys(page) >> PAGE_SHIFT;
	ret = sbi_ecall(ZION_SBI_EXT, SBI_SM_REGISTER_PT, tid,
			(unsigned long)&pt, 0, 0, 0, 0);
	if (ret.error) {
		pr_err("[CVM MANAGEMENT] FAIL: ordinary REE page returned %ld\n",
		       ret.error);
		goto out_destroy;
	}

	pt.gpa = PAGE_SIZE;
	pt.hfn = ZION_SMM_BASE >> PAGE_SHIFT;
	ret = sbi_ecall(ZION_SBI_EXT, SBI_SM_REGISTER_PT, tid,
			(unsigned long)&pt, 0, 0, 0, 0);
	if (expect_failure("SM REGISTER_PT", ret))
		goto out_destroy;
	pt.gpa = 2 * PAGE_SIZE;
	pt.hfn = core.base >> PAGE_SHIFT;
	ret = sbi_ecall(ZION_SBI_EXT, SBI_SM_REGISTER_PT, tid,
			(unsigned long)&pt, 0, 0, 0, 0);
	if (expect_failure("trusted-pool REGISTER_PT", ret))
		goto out_destroy;

	ret = sbi_ecall(ZION_SBI_EXT, SBI_SM_SYNC_PT, tid, 0,
			page_to_phys(page), 0, 0, 0);
	if (expect_failure("host page-table injection", ret))
		goto out_destroy;

	/* The 4 KiB mapping above creates a valid non-leaf at the 2 MiB level.
	 * LOAD_MEM must reject it instead of copying into the page-table page. */
	load.stash = stash;
	load.pos = 0;
	load.size = 1;
	ret = sbi_ecall(ZION_SBI_EXT, SBI_SM_LOAD_MEM, tid,
			(unsigned long)&load, 0, 0, 0, 0);
	if (expect_failure("LOAD_MEM through a non-leaf", ret))
		goto out_destroy;

	load.pos = ~0UL;
	load.size = sizeof(probe);
	ret = sbi_ecall(ZION_SBI_EXT, SBI_SM_LOAD_MEM, tid,
			(unsigned long)&load, 0, 0, 0, 0);
	if (expect_failure("overflowing LOAD_MEM range", ret))
		goto out_destroy;
	result = 0;

out_destroy:
	ret = sbi_ecall(ZION_SBI_EXT, SBI_SM_DESTROY_CVM, tid, 0, 0, 0, 0, 0);
	if (ret.error) {
		pr_err("[CVM MANAGEMENT] FAIL: scratch CVM destroy returned %ld\n",
		       ret.error);
		result = -EIO;
	}
out:
	__free_page(page);
	memzero_explicit(probe, sizeof(probe));
	return result;
}

static long cvm_management_capacity(unsigned long expected_successes)
{
	unsigned long tids[CVM_MANAGEMENT_MAX_CAPACITY] = { 0 };
	unsigned long created = 0;
	struct sbiret ret;
	long result = 0;

	if (expected_successes > CVM_MANAGEMENT_MAX_CAPACITY)
		return -EINVAL;

	while (created < CVM_MANAGEMENT_MAX_CAPACITY) {
		ret = sbi_ecall(ZION_SBI_EXT, SBI_SM_CREATE_CVM, 0, 0,
				0, 0, 0, 0);
		if (ret.error)
			break;
		tids[created++] = ret.value;
	}
	ret = sbi_ecall(ZION_SBI_EXT, SBI_SM_CREATE_CVM, 0, 0, 0, 0, 0, 0);
	if (!ret.error) {
		pr_err("[CVM MANAGEMENT] FAIL: capacity overflow created tid=%lx\n",
		       ret.value);
		if (created < CVM_MANAGEMENT_MAX_CAPACITY)
			tids[created++] = ret.value;
		result = -EINVAL;
	}
	if (!result && created != expected_successes) {
		pr_err("[CVM MANAGEMENT] FAIL: capacity created %lu, expected %lu\n",
		       created, expected_successes);
		result = -EINVAL;
	}

	while (created) {
		created--;
		ret = sbi_ecall(ZION_SBI_EXT, SBI_SM_DESTROY_CVM,
				tids[created], 0, 0, 0, 0, 0);
		if (ret.error) {
			pr_err("[CVM MANAGEMENT] FAIL: capacity destroy tid=%lu returned %ld\n",
			       tids[created], ret.error);
			result = -EIO;
		}
	}

	return result;
}

static long cvm_management_ioctl(struct file *file, unsigned int command,
				 unsigned long argument)
{
	(void)file;
	switch (command) {
	case CVM_MANAGEMENT_RUN:
		return cvm_management_run(argument);
	case CVM_MANAGEMENT_CAPACITY:
		return cvm_management_capacity(argument);
	default:
		return -ENOTTY;
	}
}

static const struct file_operations cvm_management_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = cvm_management_ioctl,
};

static struct miscdevice cvm_management_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "cvm-management-negative",
	.fops = &cvm_management_fops,
};

static int __init cvm_management_init(void)
{
	return misc_register(&cvm_management_device);
}

static void __exit cvm_management_exit(void)
{
	misc_deregister(&cvm_management_device);
}

module_init(cvm_management_init);
module_exit(cvm_management_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Zion CVM direct-SBI management validation regression");
