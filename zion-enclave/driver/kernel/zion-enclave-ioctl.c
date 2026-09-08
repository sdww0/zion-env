//******************************************************************************
// Copyright (c) 2018, The Regents of the University of California (Regents).
// All Rights Reserved. See LICENSE for license details.
//------------------------------------------------------------------------------
#include "zion-enclave.h"
#include "zion-enclave-sbi.h"
#include "zion_user.h"
#include <asm/sbi.h>
#include <linux/uaccess.h>
#include <linux/string.h>
#include <linux/sched.h>
#include <linux/limits.h>
#include <linux/jiffies.h>
#include <linux/module.h>
#include <linux/workqueue.h>

static int __zion_destroy_enclave(struct zion_file *ctx, unsigned int ueid);

#define ZION_ORPHAN_RETRY_MIN_MS 100U
#define ZION_ORPHAN_RETRY_MAX_MS 30000U

static unsigned long zion_orphan_retry_delay(unsigned int attempts)
{
  unsigned int shift = min(attempts, 8U);
  unsigned int delay = ZION_ORPHAN_RETRY_MIN_MS << shift;

  return msecs_to_jiffies(min(delay, ZION_ORPHAN_RETRY_MAX_MS));
}

static void zion_orphan_destroy_worker(struct work_struct *work)
{
  struct enclave *enclave = container_of(to_delayed_work(work),
                                         struct enclave,
                                         orphan_destroy_work);
  struct sbiret ret;
  bool module_pinned;

  ret = sbi_sm_destroy_enclave(enclave->eid);
  if (ret.error) {
    enclave->orphan_destroy_attempts++;
    if (is_power_of_2(enclave->orphan_destroy_attempts))
      zion_warn("orphan enclave %ld destroy retry %u failed: SBI error %ld\n",
                enclave->eid, enclave->orphan_destroy_attempts, ret.error);
    mod_delayed_work(system_wq, &enclave->orphan_destroy_work,
                     zion_orphan_retry_delay(
                         enclave->orphan_destroy_attempts));
    return;
  }

  zion_info("orphan enclave %ld reclaimed after %u retries\n",
            enclave->eid, enclave->orphan_destroy_attempts);
  module_pinned = enclave->orphan_module_pinned;
  enclave->orphan_module_pinned = false;
  enclave_put(enclave); /* Drop the former IDR ownership reference. */
  if (module_pinned)
    module_put(THIS_MODULE);
}

static int zion_queue_orphan_destroy(struct zion_file *ctx)
{
  struct enclave *enclave;
  unsigned int ueid = ctx->ueid;

  if (!ueid)
    return 0;
  enclave = enclave_idr_remove(ueid);
  if (!enclave)
    return -ENOENT;

  ctx->ueid = 0;
  INIT_DELAYED_WORK(&enclave->orphan_destroy_work,
                    zion_orphan_destroy_worker);
  enclave->orphan_destroy_attempts = 0;
  /* release() is still executing under a file-operations module reference,
   * so this unconditional pin is safe.  It prevents rmmod from discarding the
   * retry callback while protected memory is still owned by the monitor. */
  __module_get(THIS_MODULE);
  enclave->orphan_module_pinned = true;
  mod_delayed_work(system_wq, &enclave->orphan_destroy_work,
                   zion_orphan_retry_delay(0));
  return 0;
}

struct zion_run_work {
  unsigned long eid;
  bool resume;
  struct sbiret sbi_ret;
};

static void zion_resched_after_private_quantum(const struct sbiret *ret)
{
  if (ret->error == SBI_ERR_SM_ENCLAVE_INTERRUPTED &&
      ret->value == SBI_SM_ENCLAVE_INTERRUPT_PRIVATE_QUANTUM)
    cond_resched();
}

static long zion_run_on_target_cpu(void *opaque)
{
  struct zion_run_work *work = opaque;

  if (work->resume)
    work->sbi_ret = sbi_sm_resume_enclave(work->eid);
  else
    work->sbi_ret = sbi_sm_run_enclave(work->eid);
  zion_resched_after_private_quantum(&work->sbi_ret);
  return 0;
}

static int zion_run_sbi(unsigned long eid, bool resume,
                            struct sbiret *sbi_ret)
{
  struct zion_run_work work = {
    .eid = eid,
    .resume = resume,
  };
  long ret;

  if (zion_native_enclave_cpu < 0) {
    *sbi_ret = resume ? sbi_sm_resume_enclave(eid) :
                        sbi_sm_run_enclave(eid);
    zion_resched_after_private_quantum(sbi_ret);
    return 0;
  }

  /* Keep the complete SBI run/stop transaction on one known hart. Every
   * timer-driven resume then uses the same per-hart host context and EIC7700X
   * ACLINT compare register. */
  ret = work_on_cpu(zion_native_enclave_cpu,
                    zion_run_on_target_cpu, &work);
  if (ret)
    return (int)ret;
  *sbi_ret = work.sbi_ret;
  return 0;
}

static struct enclave *zion_get_owned_enclave(struct zion_file *ctx,
                                               unsigned long ueid,
                                               int *error)
{
  struct enclave *enclave;

  if (!ctx->ueid || ueid != ctx->ueid) {
    *error = -EPERM;
    return NULL;
  }
  enclave = enclave_get_by_id(ctx->ueid);
  if (!enclave)
    *error = -ENOENT;
  return enclave;
}

static bool zion_epm_contains(const struct epm *epm, unsigned long address,
                              unsigned long size)
{
  unsigned long offset;

  if (!size || address < epm->pa)
    return false;
  offset = address - epm->pa;
  return offset < epm->size && size <= epm->size - offset;
}

static int zion_create_enclave(struct zion_file *ctx, unsigned long arg)
{
  /* create parameters */
  struct zion_ioctl_create_enclave *enclp = (struct zion_ioctl_create_enclave *) arg;

  struct enclave *enclave;
  int ueid;

  if (ctx->ueid)
    return -EBUSY;
  if (!enclp->min_pages || enclp->min_pages > UINT_MAX)
    return -EINVAL;

  enclave = create_enclave(enclp->min_pages);

  if (enclave == NULL) {
    return -ENOMEM;
  }

  ueid = enclave_idr_alloc(enclave);
  if (ueid < 0) {
    enclave_put(enclave);
    return ueid;
  }

  ctx->ueid = ueid;
  enclp->eid = ueid;
  enclp->epm_paddr = enclave->epm->pa;
  enclp->epm_size = enclave->epm->size;
  enclave_put(enclave); /* The IDR now owns the persistent reference. */

  return 0;
}


static int zion_finalize_enclave(struct zion_file *ctx, unsigned long arg)
{
  struct sbiret ret;
  struct enclave *enclave;
  struct utm *utm;
  struct zion_sbi_create_t create_args = { 0 };

  struct zion_ioctl_create_enclave *enclp = (struct zion_ioctl_create_enclave *) arg;

  int error = 0;

  enclave = zion_get_owned_enclave(ctx, enclp->eid, &error);
  if (!enclave)
    return error;
  if (!enclave->is_init || enclave->eid >= 0) {
    error = -EBUSY;
    goto out_put;
  }
  if (!zion_epm_contains(enclave->epm, enclp->runtime_paddr, 1) ||
      !zion_epm_contains(enclave->epm, enclp->user_paddr, 1) ||
      !zion_epm_contains(enclave->epm, enclp->free_paddr,
                         enclp->free_requested)) {
    error = -ERANGE;
    goto out_put;
  }

  /* SBI Call */
  create_args.epm_region.paddr = enclave->epm->pa;
  create_args.epm_region.size = enclave->epm->size;

  utm = enclave->utm;

  if (utm) {
    create_args.utm_region.paddr = __pa(utm->ptr);
    create_args.utm_region.size = utm->size;
  } else {
    create_args.utm_region.paddr = 0;
    create_args.utm_region.size = 0;
  }

  // physical addresses for runtime, user, and freemem
  create_args.runtime_paddr = enclp->runtime_paddr;
  create_args.user_paddr = enclp->user_paddr;
  create_args.free_paddr = enclp->free_paddr;
  create_args.free_requested = enclp->free_requested;

  ret = sbi_sm_create_enclave(&create_args);

  if (ret.error) {
    zion_err("zion_create_enclave: SBI call failed with error code %ld\n", ret.error);
    error = -EINVAL;
    goto out_put;
  }

  if (ret.value < 0) {
    zion_err("zion_create_enclave: SBI returned invalid handle %ld\n",
             ret.value);
    error = -EIO;
    goto out_put;
  }

  enclave->eid = ret.value;
  enclave->is_init = false;

out_put:
  enclave_put(enclave);
  return error;

}

static int zion_run_enclave(struct zion_file *ctx, unsigned long data)
{
  struct sbiret ret;
  int rc;
  unsigned long ueid;
  struct enclave* enclave;
  struct zion_ioctl_run_enclave *arg = (struct zion_ioctl_run_enclave*) data;

  ueid = arg->eid;
  int error = 0;

  enclave = zion_get_owned_enclave(ctx, ueid, &error);
  if (!enclave)
    return error;

  if (enclave->is_init || enclave->eid < 0) {
    zion_err("real enclave does not exist\n");
    error = -EINVAL;
    goto out_put;
  }

  rc = zion_run_sbi(enclave->eid, false, &ret);
  if (rc) {
    error = rc;
    goto out_put;
  }

  arg->error = ret.error;
  arg->value = ret.value;

out_put:
  enclave_put(enclave);
  return error;
}

static int utm_init_ioctl(struct zion_file *ctx, unsigned long arg)
{
  int ret = 0;
  struct utm *utm;
  struct enclave *enclave;
  struct zion_ioctl_create_enclave *enclp = (struct zion_ioctl_create_enclave *) arg;
  long long unsigned untrusted_size = enclp->utm_size;

  enclave = zion_get_owned_enclave(ctx, enclp->eid, &ret);
  if (!enclave)
    return ret;
  if (!enclave->is_init || enclave->utm) {
    ret = -EBUSY;
    goto out_put;
  }

  utm = kzalloc(sizeof(struct utm), GFP_KERNEL);
  if (!utm) {
    ret = -ENOMEM;
    goto out_put;
  }

  ret = utm_init(utm, untrusted_size);
  if (ret) {
    kfree(utm);
    goto out_put;
  }

  enclave->utm = utm;
  enclp->utm_paddr = __pa(utm->ptr);

out_put:
  enclave_put(enclave);
  return ret;
}

static void zion_rollback_utm(struct zion_file *ctx)
{
  struct enclave *enclave;
  struct utm *utm = NULL;

  enclave = enclave_get_by_id(ctx->ueid);
  if (!enclave)
    return;
  if (enclave->is_init) {
    utm = enclave->utm;
    enclave->utm = NULL;
  }
  enclave_put(enclave);
  if (utm) {
    utm_destroy(utm);
    kfree(utm);
  }
}

static bool zion_ioctl_has_output(unsigned int cmd)
{
  switch (cmd) {
    case ZION_IOC_CREATE_ENCLAVE:
    case ZION_IOC_RUN_ENCLAVE:
    case ZION_IOC_RESUME_ENCLAVE:
    case ZION_IOC_UTM_INIT:
      return true;
    default:
      return false;
  }
}


static int zion_destroy_enclave(struct zion_file *ctx, unsigned long arg)
{
  struct zion_ioctl_create_enclave *enclp = (struct zion_ioctl_create_enclave *) arg;
  return __zion_destroy_enclave(ctx, enclp->eid);
}

static int __zion_destroy_enclave(struct zion_file *ctx, unsigned int ueid)
{
  struct sbiret ret;
  struct enclave *enclave;
  struct enclave *removed;
  int error = 0;

  enclave = zion_get_owned_enclave(ctx, ueid, &error);
  if (!enclave)
    return error;

  if (enclave->eid >= 0) {
    ret = sbi_sm_destroy_enclave(enclave->eid);
    if (ret.error) {
      zion_err("fatal: cannot destroy enclave: SBI failed with error code %ld\n", ret.error);
      error = -EIO;
      goto out_put;
    }
  }

  removed = enclave_idr_remove(ueid);
  if (WARN_ON(removed != enclave)) {
    error = -EIO;
    if (removed)
      enclave_put(removed);
    goto out_put;
  }
  ctx->ueid = 0;
  enclave_put(removed); /* Drop the IDR ownership reference. */

out_put:
  enclave_put(enclave);
  return error;
}

static int zion_resume_enclave(struct zion_file *ctx, unsigned long data)
{
  struct sbiret ret;
  int rc;
  struct zion_ioctl_run_enclave *arg = (struct zion_ioctl_run_enclave*) data;
  unsigned long ueid = arg->eid;
  struct enclave* enclave;
  int error = 0;

  enclave = zion_get_owned_enclave(ctx, ueid, &error);
  if (!enclave)
    return error;

  if (enclave->is_init || enclave->eid < 0) {
    zion_err("real enclave does not exist\n");
    error = -EINVAL;
    goto out_put;
  }

  rc = zion_run_sbi(enclave->eid, true, &ret);
  if (rc) {
    error = rc;
    goto out_put;
  }

  arg->error = ret.error;
  arg->value = ret.value;

out_put:
  enclave_put(enclave);
  return error;
}

long zion_ioctl(struct file *filep, unsigned int cmd, unsigned long arg)
{
  struct zion_file *ctx = filep->private_data;
  long ret;
  char data[512] = { 0 };

  size_t ioc_size;

  if (!ctx || !arg)
    return -EINVAL;

  ioc_size = _IOC_SIZE(cmd);
  if (!ioc_size || ioc_size > sizeof(data))
    return -EINVAL;

  if (copy_from_user(data,(void __user *) arg, ioc_size))
    return -EFAULT;

  if (mutex_lock_interruptible(&ctx->lock))
    return -ERESTARTSYS;

  switch (cmd) {
    case ZION_IOC_CREATE_ENCLAVE:
      ret = zion_create_enclave(ctx, (unsigned long) data);
      break;
    case ZION_IOC_FINALIZE_ENCLAVE:
      ret = zion_finalize_enclave(ctx, (unsigned long) data);
      break;
    case ZION_IOC_DESTROY_ENCLAVE:
      ret = zion_destroy_enclave(ctx, (unsigned long) data);
      break;
    case ZION_IOC_RUN_ENCLAVE:
      ret = zion_run_enclave(ctx, (unsigned long) data);
      break;
    case ZION_IOC_RESUME_ENCLAVE:
      ret = zion_resume_enclave(ctx, (unsigned long) data);
      break;
    /* Note that following commands could have been implemented as a part of ADD_PAGE ioctl.
     * However, there was a weird bug in compiler that generates a wrong control flow
     * that ends up with an illegal instruction if we combine switch-case and if statements.
     * We didn't identified the exact problem, so we'll have these until we figure out */
    case ZION_IOC_UTM_INIT:
      ret = utm_init_ioctl(ctx, (unsigned long) data);
      break;
    default:
      ret = -ENOTTY;
      break;
  }

  if (!ret && zion_ioctl_has_output(cmd) &&
      copy_to_user((void __user*) arg, data, ioc_size)) {
    /* Allocation ioctls must be transactional when userspace cannot receive
     * the returned handle or physical address. */
    if (cmd == ZION_IOC_CREATE_ENCLAVE && ctx->ueid)
      __zion_destroy_enclave(ctx, ctx->ueid);
    else if (cmd == ZION_IOC_UTM_INIT && ctx->ueid)
      zion_rollback_utm(ctx);
    ret = -EFAULT;
  }

  mutex_unlock(&ctx->lock);
  return ret;
}

int zion_open(struct inode *inode, struct file *file)
{
  struct zion_file *ctx;

  (void)inode;
  ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
  if (!ctx)
    return -ENOMEM;
  mutex_init(&ctx->lock);
  file->private_data = ctx;
  return 0;
}

int zion_release(struct inode *inode, struct file *file)
{
  struct zion_file *ctx = file->private_data;
  int ret = 0;

  (void)inode;
  if (!ctx)
    return 0;

  mutex_lock(&ctx->lock);
  if (ctx->ueid)
    ret = __zion_destroy_enclave(ctx, ctx->ueid);
  if (ret) {
    int orphan_ret = zion_queue_orphan_destroy(ctx);

    if (orphan_ret)
      zion_err("close could not queue enclave recovery: %d\n", orphan_ret);
  }
  mutex_unlock(&ctx->lock);
  if (ret)
    zion_err("close could not destroy enclave: %d; recovery queued\n",
             ret);
  kfree(ctx);
  file->private_data = NULL;
  /* Linux ignores release errors.  The recovery worker now owns any enclave
   * which the synchronous close path could not destroy. */
  return 0;
}
