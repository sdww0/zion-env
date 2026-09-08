//******************************************************************************
// Copyright (c) 2018, The Regents of the University of California (Regents).
// All Rights Reserved. See LICENSE for license details.
//------------------------------------------------------------------------------
//#include <asm/io.h>
//#include <asm/page.h>
#include "zion-enclave.h"
#include "zion-enclave-sbi.h"

#include <linux/dma-mapping.h>
#include <linux/mm.h>
#include <linux/file.h>
#include <linux/overflow.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/miscdevice.h>
#include <linux/cpu.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include "zion_user.h"
#define   DRV_DESCRIPTION   "Zion enclave"
#define   DRV_VERSION       "1.0.0"

#ifndef ZION_UNIFIED_DRIVER
MODULE_DESCRIPTION(DRV_DESCRIPTION);
MODULE_AUTHOR("Dayeol Lee <dayeol@berkeley.edu>");
MODULE_VERSION(DRV_VERSION);
MODULE_LICENSE("Dual BSD/GPL");
#endif

static const struct file_operations zion_fops = {
    .owner          = THIS_MODULE,
    .open           = zion_open,
    .mmap           = zion_mmap,
    .unlocked_ioctl = zion_ioctl,
    .release        = zion_release
};

struct miscdevice zion_dev = {
  .minor = MISC_DYNAMIC_MINOR,
  .name = "zion_enclave",
  .fops = &zion_fops,
  .mode = 0600,
};

static unsigned long sm_pool_base = SM_POOL_BASE_DEFAULT;
static unsigned long sm_pool_size = SM_POOL_SIZE_DEFAULT;
static bool nested_cvm;
/* Native enclaves normally run on the calling CPU.  A non-negative value is
 * retained as an opt-in diagnostic/compatibility override, not a correctness
 * requirement. */
int zion_native_enclave_cpu = -1;
module_param(sm_pool_base, ulong, 0444);
MODULE_PARM_DESC(sm_pool_base,
                 "trusted pool base when zion,trusted-memory is absent");
module_param(sm_pool_size, ulong, 0444);
MODULE_PARM_DESC(sm_pool_size,
                 "trusted pool size when zion,trusted-memory is absent");
module_param(nested_cvm, bool, 0444);
MODULE_PARM_DESC(nested_cvm,
                 "run inside a Zion CVM whose trusted pool is owned by the parent REE");
module_param_named(native_enclave_cpu, zion_native_enclave_cpu, int, 0444);
MODULE_PARM_DESC(native_enclave_cpu,
                 "optional CPU for native enclave run/resume (-1=calling CPU)");

static int zion_configure_native_enclave_cpu(void)
{
  if (zion_native_enclave_cpu < -1 ||
      (zion_native_enclave_cpu >= 0 &&
       (zion_native_enclave_cpu >= nr_cpu_ids ||
        !cpu_online(zion_native_enclave_cpu)))) {
    pr_err("zion_enclave: native_enclave_cpu=%d is not online\n",
           zion_native_enclave_cpu);
    return -EINVAL;
  }

  if (zion_native_enclave_cpu >= 0)
    pr_info("zion_enclave: native enclave run/resume pinned to CPU%d\n",
            zion_native_enclave_cpu);
  return 0;
}

static int zion_find_sm_pool(unsigned long *base, unsigned long *size)
{
  struct device_node *node;
  struct resource resource;
  resource_size_t resource_len;
  int ret;

  node = of_find_compatible_node(NULL, NULL, "zion,trusted-memory");
  if (node) {
    ret = of_address_to_resource(node, 0, &resource);
    of_node_put(node);
    if (ret) {
      pr_err("zion_enclave: invalid zion,trusted-memory resource\n");
      return ret;
    }
    resource_len = resource_size(&resource);
    if (resource.start > ULONG_MAX || resource_len > ULONG_MAX)
      return -ERANGE;
    *base = (unsigned long)resource.start;
    *size = (unsigned long)resource_len;
  } else {
    *base = sm_pool_base;
    *size = sm_pool_size;
    pr_warn("zion_enclave: no zion,trusted-memory node; using compatibility parameters\n");
  }

  /* One NAPOT entry is the only viable core-pool representation on the
   * eight-entry EIC7700X PMP. */
  if (!*size || !is_power_of_2(*size) || (*base & (*size - 1)) ||
      (*size & (PAGE_SIZE - 1))) {
    pr_err("zion_enclave: trusted pool must be page-sized, power-of-two, and naturally aligned\n");
    return -EINVAL;
  }
  return 0;
}

static void zion_vma_open(struct vm_area_struct *vma)
{
  enclave_get(vma->vm_private_data);
}

static void zion_vma_close(struct vm_area_struct *vma)
{
  enclave_put(vma->vm_private_data);
}

static const struct vm_operations_struct zion_vm_ops = {
  .open = zion_vma_open,
  .close = zion_vma_close,
};

int zion_mmap(struct file* filp, struct vm_area_struct *vma)
{
  struct zion_file *ctx = filp->private_data;
  struct enclave *enclave;
  unsigned long vsize;
  unsigned long offset;
  unsigned long region_size;
  paddr_t region_base;
  paddr_t paddr;
  int ret;

  if (!ctx)
    return -EINVAL;
  if (mutex_lock_interruptible(&ctx->lock))
    return -ERESTARTSYS;
  if (!ctx->ueid) {
    ret = -EINVAL;
    goto out_unlock;
  }
  enclave = enclave_get_by_id(ctx->ueid);
  if (!enclave) {
    ret = -ENOENT;
    goto out_unlock;
  }

  vsize = vma->vm_end - vma->vm_start;
  if (!vsize || vma->vm_pgoff > (ULONG_MAX >> PAGE_SHIFT)) {
    ret = -EINVAL;
    goto out_put;
  }
  offset = vma->vm_pgoff << PAGE_SHIFT;

  if (enclave->is_init) {
    region_base = enclave->epm->pa;
    region_size = enclave->epm->size;
  } else if (enclave->utm) {
    region_base = __pa(enclave->utm->ptr);
    region_size = enclave->utm->size;
  } else {
    ret = -ENXIO;
    goto out_put;
  }

  if (offset > region_size || vsize > region_size - offset ||
      check_add_overflow(region_base, (paddr_t)offset, &paddr)) {
    ret = -EINVAL;
    goto out_put;
  }

  vm_flags_set(vma, VM_DONTEXPAND | VM_DONTDUMP);
  ret = remap_pfn_range(vma, vma->vm_start, paddr >> PAGE_SHIFT,
                        vsize, vma->vm_page_prot);
  if (ret)
    goto out_put;

  /* Transfer this reference to the initial VMA.  vm_open takes another
   * reference for each forked or split VMA and vm_close releases it. */
  vma->vm_ops = &zion_vm_ops;
  vma->vm_private_data = enclave;
  mutex_unlock(&ctx->lock);
  return 0;

out_put:
  enclave_put(enclave);
out_unlock:
  mutex_unlock(&ctx->lock);
  return ret;
}

int zion_frontend_register(bool reserve_pool)
{
  int  ret;
  long sbi_extension;
  unsigned long pool_base;
  unsigned long pool_size;

  ret = zion_configure_native_enclave_cpu();
  if (ret)
    return ret;

  if (!nested_cvm) {
    sbi_extension = sbi_probe_extension(
        SBI_EXT_EXPERIMENTAL_ZION_ENCLAVE);
    if (sbi_extension <= 0) {
      pr_err("zion_enclave: Zion SBI extension 0x%08x is unavailable (%ld)\n",
             SBI_EXT_EXPERIMENTAL_ZION_ENCLAVE, sbi_extension);
      return -ENODEV;
    }
    pr_info("zion_enclave: Zion SBI extension 0x%08x detected\n",
            SBI_EXT_EXPERIMENTAL_ZION_ENCLAVE);
  } else {
    /* The recovered CVM KVM path forwards Zion calls but cannot emulate
     * SBI_EXT_BASE probe_extension for experimental extension IDs.  The
     * explicit nested mode is only used after the parent has established the
     * Zion monitor, so avoid that unsupported discovery call here. */
    pr_info("zion_enclave: nested CVM mode; skipping unsupported SBI extension probe\n");
  }

  /* The parent REE owns global trusted-pool management.  A driver running
   * inside a CVM may create enclaves, but the monitor deliberately rejects a
   * second RESERVE_MEM request from that less-privileged owner mode. */
  if (reserve_pool && !nested_cvm) {
    struct sbiret sret;

    ret = zion_find_sm_pool(&pool_base, &pool_size);
    if (ret)
      return ret;
    sret = sbi_sm_reserve_mem(pool_base, pool_size >> PAGE_SHIFT);
    if (sret.error) {
      pr_err("zion_enclave: SBI_SM_RESERVE_MEM failed (error=%ld)\n",
             sret.error);
      return -EIO;
    }
    pr_info("zion_enclave: SM pool reserved at 0x%lx (%lu MB)\n",
            pool_base, pool_size / 1024 / 1024);
  } else if (nested_cvm) {
    pr_info("zion_enclave: nested CVM mode; using parent-owned SM pool\n");
  } else {
    pr_info("zion_enclave: unified host mode; using shared Zion pool\n");
  }

  ret = misc_register(&zion_dev);
  if (ret < 0)
  {
    pr_err("zion_enclave: misc_register() failed\n");
    return ret;
  }

  /* EIC7700X places its default CMA pool above 4 GiB.  The Zion
   * frontend is a software endpoint, not a 32-bit DMA engine, and passes
   * physical EPM addresses directly to the monitor.  Give both coherent
   * and streaming allocations the platform's full physical address width
   * so dma_alloc_coherent() can use that CMA pool. */
  ret = dma_coerce_mask_and_coherent(zion_dev.this_device,
                                     DMA_BIT_MASK(64));
  if (ret) {
    pr_err("zion_enclave: could not configure 64-bit DMA mask (%d)\n",
           ret);
    misc_deregister(&zion_dev);
    return ret;
  }

  pr_info("zion_enclave: " DRV_DESCRIPTION " v" DRV_VERSION "\n");
  return ret;
}

void zion_frontend_unregister(void)
{
  pr_info("zion_enclave: zion_dev_exit()\n");
  misc_deregister(&zion_dev);
  return;
}

#ifndef ZION_UNIFIED_DRIVER
static int __init zion_dev_init(void)
{
  return zion_frontend_register(true);
}

static void __exit zion_dev_exit(void)
{
  zion_frontend_unregister();
}

module_init(zion_dev_init);
module_exit(zion_dev_exit);
#endif
