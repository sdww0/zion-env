//******************************************************************************
// Copyright (c) 2018, The Regents of the University of California (Regents).
// All Rights Reserved. See LICENSE for license details.
//------------------------------------------------------------------------------
#ifndef _ZION_ENCLAVE_H_
#define _ZION_ENCLAVE_H_

#include <asm/sbi.h>
#include <asm/csr.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/uaccess.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/idr.h>
#include <linux/mutex.h>
#include <linux/refcount.h>
#include <linux/workqueue.h>

#include <linux/file.h>

/* IMPORTANT: This code assumes Sv39 */
#include "riscv64.h"

#define PAGE_UP(addr)	(((addr)+((PAGE_SIZE)-1))&(~((PAGE_SIZE)-1)))

typedef uintptr_t vaddr_t;
typedef uintptr_t paddr_t;

extern struct miscdevice zion_dev;
extern int zion_native_enclave_cpu;
int zion_frontend_register(bool reserve_pool);
void zion_frontend_unregister(void);

long zion_ioctl(struct file* filep, unsigned int cmd, unsigned long arg);
int zion_open(struct inode *inode, struct file *file);
int zion_release(struct inode *inode, struct file *file);
int zion_mmap(struct file *filp, struct vm_area_struct *vma);

struct zion_file {
  struct mutex lock;
  unsigned int ueid;
};

/* enclave private memory */
struct epm {
  pte_t* root_page_table;
  vaddr_t ptr;
  size_t size;
  unsigned long order;
  paddr_t pa;
  bool is_cma;
  /* CMA may expose an aligned subrange to Eyrie.  Retain the complete DMA
   * allocation so epm_destroy() can release the exact original object. */
  void* cma_alloc_ptr;
  paddr_t cma_alloc_pa;
  size_t cma_alloc_size;
};

struct utm {
  pte_t* root_page_table;
  void* ptr;
  size_t size;
  unsigned long order;
};


struct enclave
{
  long eid;
  struct utm* utm;
  struct epm* epm;
  bool is_init;
  refcount_t refs;
  struct delayed_work orphan_destroy_work;
  unsigned int orphan_destroy_attempts;
  bool orphan_module_pinned;
};


// global debug functions
void debug_dump(char* ptr, unsigned long size);

// runtime/app loader
int zion_rtld_init_runtime(struct enclave* enclave, void* __user rt_ptr, size_t rt_sz, unsigned long rt_stack_sz, unsigned long* rt_offset);

int zion_rtld_init_app(struct enclave* enclave, void* __user app_ptr, size_t app_sz, size_t app_stack_sz, unsigned long stack_offset);

// untrusted memory mapper
int zion_rtld_init_untrusted(struct enclave* enclave, void* untrusted_ptr, size_t untrusted_size);

struct enclave* enclave_get_by_id(unsigned int ueid);
void enclave_get(struct enclave *enclave);
void enclave_put(struct enclave *enclave);
struct enclave* create_enclave(unsigned long min_pages);

int enclave_idr_alloc(struct enclave* enclave);
struct enclave* enclave_idr_remove(unsigned int ueid);

static inline uintptr_t  epm_satp(struct epm* epm) {
  return ((uintptr_t)epm->root_page_table >> RISCV_PGSHIFT | SATP_MODE_CHOICE);
}

int epm_destroy(struct epm* epm);
int epm_init(struct epm* epm, unsigned int count);
int utm_destroy(struct utm* utm);
int utm_init(struct utm* utm, size_t untrusted_size);
paddr_t epm_va_to_pa(struct epm* epm, vaddr_t addr);

#define zion_info(fmt, ...) \
  pr_info("zion_enclave: " fmt, ##__VA_ARGS__)
#define zion_err(fmt, ...) \
  pr_err("zion_enclave: " fmt, ##__VA_ARGS__)
#define zion_warn(fmt, ...) \
  pr_warn("zion_enclave: " fmt, ##__VA_ARGS__)
#endif
