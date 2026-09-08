#include "mm/freemem.h"
#include "mm/common.h"
#include "mm/vm_defs.h"
#include "util/string.h"

static uintptr_t freeBase;
static uintptr_t freeEnd;
static uintptr_t freeStart;

void spa_init(uintptr_t base, size_t size)
{
  assert(IS_ALIGNED(base, RISCV_PAGE_BITS));
  assert(IS_ALIGNED(size, RISCV_PAGE_BITS));
  assert(size <= UINTPTR_MAX - base);
  freeStart = base;
  freeBase = base;
  freeEnd = freeBase + size;
}

uintptr_t spa_get()
{
  return spa_get_zero(); // not allowed, so change to safe
}

uintptr_t spa_get_zero()
{
  if (freeBase >= freeEnd) {
    return 0;
  }
  uintptr_t new_page = freeBase;
  memset((void *) new_page, 0, RISCV_PAGE_SIZE);

  freeBase += RISCV_PAGE_SIZE;
  return new_page;
}

void spa_put(uintptr_t page)
{
  /* The loader uses a bump allocator. Page-table creation rolls back in
   * reverse allocation order, so only the most recent page is returnable. */
  assert(page <= UINTPTR_MAX - RISCV_PAGE_SIZE);
  assert(page + RISCV_PAGE_SIZE == freeBase);
  freeBase = page;
}

unsigned int spa_available()
{
  return (freeEnd - freeBase) / RISCV_PAGE_SIZE;
}

bool spa_owns(uintptr_t page)
{
  return IS_ALIGNED(page, RISCV_PAGE_BITS) && page >= freeStart &&
         page < freeEnd;
}
