#include "mm/common.h"
#include "mm/mm.h"
#include "mm/vm.h"
#include "mm/freemem.h"
#include "mm/paging.h"

/* Hacky storage of current u-mode break */
static uintptr_t current_program_break;
static uintptr_t minimum_program_break;

uintptr_t get_program_break()
{
  return current_program_break;
}

void set_program_break(uintptr_t new_break)
{
  if (minimum_program_break == 0)
    minimum_program_break = new_break;
  current_program_break = new_break;
}

uintptr_t get_program_break_minimum()
{
  return minimum_program_break;
}

static pte*
__walk_internal(pte* root, uintptr_t addr, int create)
{
  pte* t = root;
  pte* created_entries[RISCV_PT_LEVELS - 1];
  uintptr_t created_pages[RISCV_PT_LEVELS - 1];
  size_t created_count = 0;
  int i;
  for (i = 1; i < RISCV_PT_LEVELS; i++)
  {
    size_t idx = RISCV_GET_PT_INDEX(addr, i);

    if (!(t[idx] & PTE_V)) {
      uintptr_t new_page;

      if (!create)
        return 0;
      new_page = spa_get_zero();
      if (!new_page)
        goto rollback;
      created_entries[created_count] = &t[idx];
      created_pages[created_count] = new_page;
      created_count++;
      t[idx] = ptd_create(ppn(__pa(new_page)));
    }

    /* A valid R/W/X entry is a leaf (possibly a huge page), not another
     * page-table level. Never reinterpret its mapped data as PTEs. */
    if (t[idx] & (PTE_R | PTE_W | PTE_X))
      goto rollback;

    t = (pte*) __va(pte_ppn(t[idx]) << RISCV_PAGE_BITS);
  }

  return &t[RISCV_GET_PT_INDEX(addr, RISCV_PT_LEVELS)];

rollback:
  while (created_count > 0) {
    created_count--;
    *created_entries[created_count] = 0;
    spa_put(created_pages[created_count]);
  }
  return 0;
}

/* walk the page table and return PTE
 * return 0 if no mapping exists */
static pte*
__walk(pte* root, uintptr_t addr)
{
  return __walk_internal(root, addr, 0);
}

/* walk the page table and return PTE
 * create the mapping if non exists */
static pte*
__walk_create(pte* root, uintptr_t addr)
{
  return __walk_internal(root, addr, 1);
}

#ifndef LOADER_BIN
static int
page_table_empty(const pte* table)
{
  size_t i;
  for (i = 0; i < BIT(RISCV_PT_INDEX_BITS); ++i) {
    if (table[i] != 0)
      return 0;
  }
  return 1;
}
#endif

/* Create a virtual memory mapping between a physical and virtual page */
uintptr_t 
map_page(uintptr_t vpn, uintptr_t ppn, int flags)
{
  if (vpn > (UINTPTR_MAX >> RISCV_PAGE_BITS))
    return 0;

  pte* pte = __walk_create(root_page_table, vpn << RISCV_PAGE_BITS);

  if (!pte)
    return 0;

  if (*pte != 0) {
    return 0;
  }

  *pte = pte_create(ppn, PTE_D | PTE_A | PTE_V | flags);
  return 1;
}

/* allocate a new page to a given vpn
 * returns VA of the page, (returns 0 if fails) */
uintptr_t
alloc_page(uintptr_t vpn, int flags)
{
  uintptr_t page;

  if (vpn > (UINTPTR_MAX >> RISCV_PAGE_BITS))
    return 0;

  pte* pte = __walk(root_page_table, vpn << RISCV_PAGE_BITS);

  if (pte && *pte != 0)
    return 0;

  page = spa_get_zero();
  if (!page)
    return 0;

  pte = __walk_create(root_page_table, vpn << RISCV_PAGE_BITS);
  if (!pte || *pte != 0) {
    spa_put(page);
    return 0;
  }

  if (flags & (PTE_R | PTE_W | PTE_X))
    *pte = pte_create(ppn(__pa(page)), PTE_D | PTE_A | flags);
  else {
    assert(flags & PTE_U);
    *pte = pte_create_prot_none(ppn(__pa(page)));
  }
#ifdef USE_PAGING
  paging_inc_user_page();
#endif

  return page;
}

uintptr_t
realloc_page(uintptr_t vpn, int flags)
{
  assert(flags & PTE_U);

  if (vpn > (UINTPTR_MAX >> RISCV_PAGE_BITS))
    return 0;

  pte *pte = __walk(root_page_table, vpn << RISCV_PAGE_BITS);
  if(!pte)
    return 0;

  if (((*pte & (PTE_V | PTE_U)) == (PTE_V | PTE_U) &&
       (*pte & (PTE_R | PTE_W | PTE_X))) ||
      pte_is_prot_none(*pte)) {
    uintptr_t old_ppn = pte_ppn(*pte);
    if (flags & (PTE_R | PTE_W | PTE_X))
      *pte = pte_create(old_ppn, flags);
    else
      *pte = pte_create_prot_none(old_ppn);
    return __va(old_ppn << RISCV_PAGE_BITS);
  }

  return 0;
}

int
free_page(uintptr_t vpn)
{
  if (vpn > (UINTPTR_MAX >> RISCV_PAGE_BITS))
    return 0;
  uintptr_t addr = vpn << RISCV_PAGE_BITS;
  pte* table = root_page_table;
#ifndef LOADER_BIN
  pte* parent_entries[RISCV_PT_LEVELS - 1];
  pte* child_tables[RISCV_PT_LEVELS - 1];
  size_t depth = 0;
#endif
  int level;

  for (level = 1; level < RISCV_PT_LEVELS; ++level) {
    size_t idx = RISCV_GET_PT_INDEX(addr, level);
    pte* entry = &table[idx];
    if (!(*entry & PTE_V) || (*entry & (PTE_R | PTE_W | PTE_X)))
      return 0;
#ifndef LOADER_BIN
    parent_entries[depth] = entry;
#endif
    table = (pte*)__va(pte_ppn(*entry) << RISCV_PAGE_BITS);
#ifndef LOADER_BIN
    child_tables[depth] = table;
    depth++;
#endif
  }

  pte* leaf_entry = &table[RISCV_GET_PT_INDEX(addr, RISCV_PT_LEVELS)];
  if (((*leaf_entry & (PTE_V | PTE_U)) != (PTE_V | PTE_U) ||
       !(*leaf_entry & (PTE_R | PTE_W | PTE_X))) &&
      !pte_is_prot_none(*leaf_entry))
    return 0;

  uintptr_t ppn = pte_ppn(*leaf_entry);
  uintptr_t page = __va(ppn << RISCV_PAGE_BITS);
  if (!spa_owns(page))
    return 0;
  // Mark invalid
  // TODO maybe do more here
  *leaf_entry = 0;

#ifdef USE_PAGING
  paging_dec_user_page();
#endif
  // Return phys page
  spa_put(page);

#ifndef LOADER_BIN
  /* Page tables allocated by the runtime also come from the SPA. Reclaim an
   * empty chain from the leaf upward, but never return loader-reserved tables. */
  while (depth > 0) {
    pte* child;
    depth--;
    child = child_tables[depth];
    if (!page_table_empty(child) || !spa_owns((uintptr_t)child))
      break;
    *parent_entries[depth] = 0;
    spa_put((uintptr_t)child);
  }
#endif

  return 1;

}

/* allocate n new pages from a given vpn
 * returns the number of pages allocated */
size_t
alloc_pages(uintptr_t vpn, size_t count, int flags)
{
  size_t i;
  for (i = 0; i < count; i++) {
    if (vpn > UINTPTR_MAX - i || !alloc_page(vpn + i, flags))
      break;
  }

  if (i != count) {
    while (i > 0) {
      i--;
      free_page(vpn + i);
    }
    return 0;
  }
  return count;
}

size_t
free_pages(uintptr_t vpn, size_t count){
  size_t i;
  size_t freed = 0;
  for (i = 0; i < count; i++) {
    if (vpn > UINTPTR_MAX - i ||
        vpn + i > (UINTPTR_MAX >> RISCV_PAGE_BITS))
      break;
    freed += free_page(vpn + i) != 0;
  }
  return freed;
}

/*
 * Check if a range of VAs contains any allocated pages, starting with
 * the given VA. Returns the number of sequential pages that meet the
 * conditions.
 */
size_t
test_va_range(uintptr_t vpn, size_t count){

  size_t i;
  /* Validate the region */
  for (i = 0; i < count; i++) {
    if (vpn > UINTPTR_MAX - i ||
        vpn + i > (UINTPTR_MAX >> RISCV_PAGE_BITS))
      break;
    pte* pte = __walk_internal(root_page_table, (vpn+i) << RISCV_PAGE_BITS, 0);
    // If the page exists and is valid then we cannot use it
    if(pte && *pte){
      break;
    }
  }
  return i;
}

/* get a mapped physical address for a VA */
uintptr_t
translate(uintptr_t va)
{
  pte* pte = __walk(root_page_table, va);

  if(pte && (*pte & PTE_V))
    return (pte_ppn(*pte) << RISCV_PAGE_BITS) | (RISCV_PAGE_OFFSET(va));
  else
    return 0;
}

/* try to retrieve PTE for a VA, return 0 if fail */
pte*
pte_of_va(uintptr_t va)
{
  pte* pte = __walk(root_page_table, va);
  return pte;
}


void
__map_with_reserved_page_table_32(uintptr_t dram_base,
                               uintptr_t dram_size,
                               uintptr_t ptr,
                               pte* l2_pt)
{
  uintptr_t offset = 0;
  uintptr_t leaf_level = 2;
  pte* leaf_pt = l2_pt;
  unsigned long dram_max =  RISCV_GET_LVL_PGSIZE(leaf_level - 1);

  /* use megapage if l2_pt is null */
  if (!l2_pt) {
    leaf_level = 1;
    leaf_pt = root_page_table;
    dram_max = -1UL; 
  }

  assert(dram_size <= dram_max);
  assert(IS_ALIGNED(dram_base, RISCV_GET_LVL_PGSIZE_BITS(leaf_level)));
  assert(IS_ALIGNED(ptr, RISCV_GET_LVL_PGSIZE_BITS(leaf_level - 1)));

  if(l2_pt) {
       /* set root page table entry */
       root_page_table[RISCV_GET_PT_INDEX(ptr, 1)] =
       ptd_create(ppn(kernel_va_to_pa(l2_pt)));
  }

  for (offset = 0;
       offset < dram_size;
       offset += RISCV_GET_LVL_PGSIZE(leaf_level))
  {
        leaf_pt[RISCV_GET_PT_INDEX(ptr + offset, leaf_level)] =
        pte_create(ppn(dram_base + offset),
                 PTE_R | PTE_W | PTE_X | PTE_A | PTE_D);
  }

}

void
__map_with_reserved_page_table_64(uintptr_t dram_base,
                               uintptr_t dram_size,
                               uintptr_t ptr,
                               pte* l2_pt,
                               pte* l3_pt)
{
  uintptr_t offset = 0;
  uintptr_t leaf_level = 3;
  pte* leaf_pt = l3_pt;
  /* use megapage if l3_pt is null */
  if (!l3_pt) {
    leaf_level = 2;
    leaf_pt = l2_pt;
  }
  assert(dram_size <= RISCV_GET_LVL_PGSIZE(leaf_level - 1));
  assert(IS_ALIGNED(dram_base, RISCV_GET_LVL_PGSIZE_BITS(leaf_level)));
  assert(IS_ALIGNED(ptr, RISCV_GET_LVL_PGSIZE_BITS(leaf_level - 1)));

  /* set root page table entry */
  root_page_table[RISCV_GET_PT_INDEX(ptr, 1)] =
    ptd_create(ppn(kernel_va_to_pa(l2_pt)));

  /* set L2 if it's not leaf */
  if (leaf_pt != l2_pt) {
    l2_pt[RISCV_GET_PT_INDEX(ptr, 2)] =
      ptd_create(ppn(kernel_va_to_pa(l3_pt)));
  }

  /* set leaf level */
  for (offset = 0;
       offset < dram_size;
       offset += RISCV_GET_LVL_PGSIZE(leaf_level))
  {
    leaf_pt[RISCV_GET_PT_INDEX(ptr + offset, leaf_level)] =
      pte_create(ppn(dram_base + offset),
          PTE_R | PTE_W | PTE_X | PTE_A | PTE_D);
  }

}

void
map_with_reserved_page_table(uintptr_t dram_base,
                             uintptr_t dram_size,
                             uintptr_t ptr,
                             pte* l2_pt,
                             pte* l3_pt)
{
  #if __riscv_xlen == 64
  if (dram_size > RISCV_GET_LVL_PGSIZE(2))
    __map_with_reserved_page_table_64(dram_base, dram_size, ptr, l2_pt, 0);
  else
    __map_with_reserved_page_table_64(dram_base, dram_size, ptr, l2_pt, l3_pt);
  #elif __riscv_xlen == 32
  if (dram_size > RISCV_GET_LVL_PGSIZE(1))
    __map_with_reserved_page_table_32(dram_base, dram_size, ptr, 0);
  else
    __map_with_reserved_page_table_32(dram_base, dram_size, ptr, l2_pt);
  #endif
}
