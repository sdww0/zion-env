#include "loader/layout.h"

#include <elf.h>
#include <stddef.h>
#include <stdint.h>

#include "mm/vm_defs.h"

static int page_up_safe(uintptr_t value, uintptr_t *rounded)
{
  if (rounded == NULL || value > UINTPTR_MAX - (RISCV_PAGE_SIZE - 1))
    return -1;
  *rounded = (value + RISCV_PAGE_SIZE - 1) &
             ~((uintptr_t)RISCV_PAGE_SIZE - 1);
  return 0;
}

static int load_page_range(elf_t *elf, size_t index, uintptr_t *start,
                           uintptr_t *end)
{
  uintptr_t memory_start = elf_getProgramHeaderVaddr(elf, index);
  size_t memory_size = elf_getProgramHeaderMemorySize(elf, index);
  uintptr_t memory_end;

  if (memory_size == 0 || memory_start > UINTPTR_MAX - memory_size)
    return -1;
  memory_end = memory_start + memory_size;
  *start = memory_start & ~((uintptr_t)RISCV_PAGE_SIZE - 1);
  return page_up_safe(memory_end, end);
}

int eyrie_elf_user_layout(elf_t *elf, uintptr_t *program_break)
{
  uintptr_t entry;
  uintptr_t highest_end = 0;
  size_t load_count = 0;
  int entry_is_executable = 0;

  if (elf == NULL || program_break == NULL)
    return -1;
  entry = elf_getEntryPoint(elf);

  for (size_t i = 0; i < elf_getNumProgramHeaders(elf); ++i) {
    uintptr_t start;
    uintptr_t memory_end;
    uintptr_t file_end;
    uintptr_t rounded_end;
    size_t file_size;
    size_t memory_size;
    size_t offset;
    size_t alignment;
    uint32_t flags;

    if (elf_getProgramHeaderType(elf, i) != PT_LOAD)
      continue;
    start = elf_getProgramHeaderVaddr(elf, i);
    file_size = elf_getProgramHeaderFileSize(elf, i);
    memory_size = elf_getProgramHeaderMemorySize(elf, i);
    offset = elf_getProgramHeaderOffset(elf, i);
    alignment = elf_getProgramHeaderAlign(elf, i);
    flags = elf_getProgramHeaderFlags(elf, i);

    if (memory_size == 0) {
      if (file_size != 0)
        return -1;
      continue;
    }
    if (file_size > memory_size ||
        start < EYRIE_USER_PROGRAM_START || start >= EYRIE_BRK_REGION_END ||
        memory_size > EYRIE_BRK_REGION_END - start ||
        offset > elf->elfSize || file_size > elf->elfSize - offset ||
        (start & (RISCV_PAGE_SIZE - 1)) !=
            (offset & (RISCV_PAGE_SIZE - 1)) ||
        (flags & (PF_R | PF_W | PF_X)) == 0 ||
        (alignment != 0 &&
         ((alignment & (alignment - 1)) != 0 ||
          ((start - offset) & (alignment - 1)) != 0)))
      return -1;

    memory_end = start + memory_size;
    file_end = start + file_size;
    if (file_end < start || file_end > memory_end ||
        page_up_safe(memory_end, &rounded_end) != 0 ||
        rounded_end > EYRIE_BRK_REGION_END)
      return -1;

    if ((flags & PF_X) != 0 && entry >= start && entry < file_end)
      entry_is_executable = 1;
    if (memory_end > highest_end)
      highest_end = memory_end;
    ++load_count;
  }

  if (load_count == 0 || !entry_is_executable ||
      page_up_safe(highest_end, program_break) != 0 ||
      *program_break < EYRIE_USER_PROGRAM_START ||
      *program_break > EYRIE_BRK_REGION_END)
    return -1;

  /* The page mapper cannot merge permissions or contents from two loadable
   * segments. Reject page-level overlap up front instead of failing after a
   * partial load. */
  for (size_t i = 0; i < elf_getNumProgramHeaders(elf); ++i) {
    uintptr_t first_start;
    uintptr_t first_end;

    if (elf_getProgramHeaderType(elf, i) != PT_LOAD ||
        elf_getProgramHeaderMemorySize(elf, i) == 0)
      continue;
    if (load_page_range(elf, i, &first_start, &first_end) != 0)
      return -1;
    for (size_t j = i + 1; j < elf_getNumProgramHeaders(elf); ++j) {
      uintptr_t second_start;
      uintptr_t second_end;

      if (elf_getProgramHeaderType(elf, j) != PT_LOAD ||
          elf_getProgramHeaderMemorySize(elf, j) == 0)
        continue;
      if (load_page_range(elf, j, &second_start, &second_end) != 0 ||
          (first_start < second_end && second_start < first_end))
        return -1;
    }
  }

  return 0;
}
