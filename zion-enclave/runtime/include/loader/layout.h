#ifndef _EYRIE_LOADER_LAYOUT_H_
#define _EYRIE_LOADER_LAYOUT_H_

#include <stdint.h>

#include "loader/elf.h"

/* Validate every loadable EAPP segment against the user program region and
 * return the page-aligned initial program break. */
int eyrie_elf_user_layout(elf_t *elf, uintptr_t *program_break);

#endif
