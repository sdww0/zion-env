//******************************************************************************
// Copyright (c) 2018, The Regents of the University of California (Regents).
// All Rights Reserved. See LICENSE for license details.
//------------------------------------------------------------------------------
#include "riscv64.h"
#include <linux/kernel.h>
#include "zion-enclave.h"
#include <linux/dma-mapping.h>
#include <linux/overflow.h>
#include <linux/sizes.h>
#include <linux/version.h>

#define EYRIE_MEGAPAGE_SIZE (2UL * 1024 * 1024)
#define ZION_MAX_EPM_SIZE SZ_1G
#define ZION_MAX_UTM_SIZE SZ_1G

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 8, 0)
#define ZION_MAX_BUDDY_ORDER MAX_PAGE_ORDER
#else
#define ZION_MAX_BUDDY_ORDER MAX_ORDER
#endif

/* Destroy all memory associated with an EPM */
int epm_destroy(struct epm* epm) {

  if(!epm->ptr || !epm->size)
    return 0;

  /* free the EPM hold by the enclave */
  if (epm->is_cma) {
    dma_free_coherent(zion_dev.this_device,
        epm->cma_alloc_size,
        epm->cma_alloc_ptr,
        epm->cma_alloc_pa);
  } else {
    free_pages(epm->ptr, epm->order);
  }

  epm->ptr = 0;
  epm->size = 0;

  return 0;
}

/* Create an EPM and initialize the free list */
int epm_init(struct epm* epm, unsigned int min_pages)
{
  vaddr_t epm_vaddr = 0;
  unsigned long order = 0;
  unsigned long count;
  size_t requested_size;
  phys_addr_t device_phys_addr = 0;
  void *cma_alloc_ptr = NULL;
  dma_addr_t cma_alloc_pa = 0;
  size_t cma_alloc_size = 0;

  if (!min_pages || min_pages > (ZION_MAX_EPM_SIZE >> PAGE_SHIFT) ||
      check_shl_overflow((size_t)min_pages, PAGE_SHIFT, &requested_size))
    return -EINVAL;

  memset(epm, 0, sizeof(*epm));
  order = get_order(requested_size);
  if (order >= BITS_PER_LONG)
    return -E2BIG;
  count = 1UL << order;

  /* prevent kernel from complaining about an invalid argument */
  if (order < ZION_MAX_BUDDY_ORDER)
    epm_vaddr = (vaddr_t) __get_free_pages(GFP_HIGHUSER, order);

#ifdef CONFIG_CMA
  /* If buddy allocator fails, we fall back to the CMA */
  if (!epm_vaddr) {
    size_t align_offset;

    epm->is_cma = 1;
    count = min_pages;
    if (check_add_overflow(requested_size,
                           EYRIE_MEGAPAGE_SIZE - PAGE_SIZE,
                           &cma_alloc_size))
      return -EOVERFLOW;

    cma_alloc_ptr = dma_alloc_coherent(zion_dev.this_device,
      cma_alloc_size,
      &cma_alloc_pa,
      GFP_KERNEL);
    if (cma_alloc_ptr) {
      align_offset = ALIGN(cma_alloc_pa, EYRIE_MEGAPAGE_SIZE) -
                     cma_alloc_pa;
      epm_vaddr = (vaddr_t)cma_alloc_ptr + align_offset;
      device_phys_addr = cma_alloc_pa + align_offset;
      epm->cma_alloc_ptr = cma_alloc_ptr;
      epm->cma_alloc_pa = cma_alloc_pa;
      epm->cma_alloc_size = cma_alloc_size;
    }
  }
#endif

  if(!epm_vaddr) {
    zion_err("failed to allocate %lu page(s)\n", count);
    return -ENOMEM;
  }

  /* zero out */
  memset((void*)epm_vaddr, 0,
         epm->is_cma ? requested_size : (size_t)count << PAGE_SHIFT);

  epm->root_page_table = (void*)epm_vaddr;
  epm->pa = (epm->is_cma) ? device_phys_addr : __pa(epm_vaddr);
  epm->order = order;
  epm->size = epm->is_cma ? requested_size : count << PAGE_SHIFT;
  epm->ptr = epm_vaddr;

  return 0;
}

int utm_destroy(struct utm* utm){

  if(utm->ptr != NULL){
    free_pages((vaddr_t)utm->ptr, utm->order);
  }

  utm->ptr = NULL;
  utm->size = 0;

  return 0;
}

int utm_init(struct utm* utm, size_t untrusted_size)
{
  size_t aligned_size;
  size_t allocated_size;
  unsigned long order;
  unsigned long count;

  if (!untrusted_size || untrusted_size > ZION_MAX_UTM_SIZE ||
      check_add_overflow(untrusted_size, PAGE_SIZE - 1, &aligned_size))
    return -EINVAL;
  aligned_size &= PAGE_MASK;
  order = get_order(aligned_size);
  if (order >= ZION_MAX_BUDDY_ORDER || order >= BITS_PER_LONG)
    return -E2BIG;
  count = 1UL << order;

  memset(utm, 0, sizeof(*utm));
  utm->order = order;

  /* Currently, UTM does not utilize CMA.
   * It is always allocated from the buddy allocator */
  utm->ptr = (void*) __get_free_pages(GFP_HIGHUSER, order);
  if (!utm->ptr) {
    zion_err("failed to allocate UTM (size = %zu bytes)\n",
             (size_t)count << PAGE_SHIFT);
    return -ENOMEM;
  }

  allocated_size = (size_t)count << PAGE_SHIFT;
  /* The buddy order is an allocator implementation detail.  Only expose the
   * page-aligned range requested by userspace to mmap and to the monitor. */
  utm->size = aligned_size;
  memset(utm->ptr, 0, allocated_size);
  if (aligned_size != untrusted_size) {
    /* Instead of failing, we just warn that the user has to fix the parameter. */
    zion_warn("shared buffer size is not multiple of PAGE_SIZE\n");
  }

  return 0;
}
