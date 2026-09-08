#include "loader/loader.h"
#include "loader/elf.h"
#include "string.h"
#include "mm/mm.h"
#include "mm/common.h"
#include "mm/vm_defs.h"
#include "mm/vm.h"

static inline int pt_mode_from_elf(int elf_pt_mode) {
  return 
    (((elf_pt_mode & PF_X) > 0) * PTE_X) |
    (((elf_pt_mode & PF_W) > 0) * (PTE_W | PTE_R | PTE_D)) |
    (((elf_pt_mode & PF_R) > 0) * PTE_R)
  ;
}

int loadElf(elf_t* elf, bool user) {
  for (unsigned int i = 0; i < elf_getNumProgramHeaders(elf); i++) {
    size_t file_remaining;
    size_t memory_size;
    size_t first_copy;

    if (elf_getProgramHeaderType(elf, i) != PT_LOAD) {
      continue;
    }

    uintptr_t start      = elf_getProgramHeaderVaddr(elf, i);
    size_t file_size     = elf_getProgramHeaderFileSize(elf, i);
    uintptr_t memory_end;
    char* src            = (char*)(elf_getProgramSegment(elf, i));
    uintptr_t va         = start;
    int pt_mode          = pt_mode_from_elf(elf_getProgramHeaderFlags(elf, i));
    pt_mode             |= (user > 0) * PTE_U;

    memory_size = elf_getProgramHeaderMemorySize(elf, i);
    if (memory_size == 0)
      continue;
    if (src == NULL || file_size > memory_size ||
        start > UINTPTR_MAX - memory_size || start > UINTPTR_MAX - file_size)
      return -1;
    memory_end = start + memory_size;
    file_remaining = file_size;

    /* va is not page-aligned, so it doesn't own some of the page. Page may already be mapped. */
    if (RISCV_PAGE_OFFSET(va)) {
      if (RISCV_PAGE_OFFSET(va) != RISCV_PAGE_OFFSET((uintptr_t) src)) {
        printf("loadElf: va and src are misaligned");
        return -1;
      }
      uintptr_t new_page = alloc_page(vpn(va), pt_mode);
      if (!new_page)
        return -1;
      first_copy = RISCV_PAGE_SIZE - RISCV_PAGE_OFFSET(va);
      if (first_copy > file_remaining)
        first_copy = file_remaining;
      if (first_copy != 0)
        memcpy((void *)(new_page + RISCV_PAGE_OFFSET(va)), src, first_copy);
      file_remaining -= first_copy;
      va = PAGE_DOWN(va) + RISCV_PAGE_SIZE;
      src += first_copy;
    }

    /* Map complete file-backed pages without copying. */
    while (file_remaining >= RISCV_PAGE_SIZE) {
      uintptr_t src_pa = __pa((uintptr_t) src);
      if (!map_page(vpn(va), ppn(src_pa), pt_mode))
        return -1;
      src += RISCV_PAGE_SIZE;
      va += RISCV_PAGE_SIZE;
      file_remaining -= RISCV_PAGE_SIZE;
    }

    /* Copy a final partial file page into zeroed memory. */
    if (file_remaining != 0) {
      uintptr_t new_page = alloc_page(vpn(va), pt_mode);
      if (!new_page)
        return -1;
      memcpy((void *)new_page, src, file_remaining);
      va += RISCV_PAGE_SIZE;
      file_remaining = 0;
    }

    /* Allocate the remaining zero-filled .bss pages. */
    while (va < memory_end) {
      uintptr_t new_page = alloc_page(vpn(va), pt_mode);
      if (!new_page)
        return -1;
      va += RISCV_PAGE_SIZE;
    }
  }

   return 0;
}

// assumes beginning and next file are page-aligned
static inline void freeUnusedElf(elf_t* elf) {
  assert(false); // TODO: needs free to be implemented properly
  for (unsigned int i = 0; i < elf_getNumProgramHeaders(elf); i++) {
    uintptr_t start      = elf_getProgramHeaderVaddr(elf, i);
    uintptr_t file_end   = start + elf_getProgramHeaderFileSize(elf, i);
    uintptr_t src        = (uintptr_t) elf_getProgramSegment(elf, i);

    if (elf_getProgramHeaderType(elf, i) != PT_LOAD) {
      uintptr_t src_end = file_end - start + src;
      for (; src < src_end; src += RISCV_PAGE_SIZE) {
        // free_page(vpn(src));
      }
      continue;
    }

    if (RISCV_PAGE_OFFSET(start)) {
      // free_page(vpn(start));
    }

    if (RISCV_PAGE_OFFSET(file_end)) {
      // free_page(vpn(file_end));
    }
  }
}
