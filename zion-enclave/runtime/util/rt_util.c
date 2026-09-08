//******************************************************************************
// Copyright (c) 2018, The Regents of the University of California (Regents).
// All Rights Reserved. See LICENSE for license details.
//------------------------------------------------------------------------------
#include "mm/mm.h"
#include "util/rt_util.h"
#include "util/printf.h"
#include "uaccess.h"
#include "mm/vm.h"

// Statically allocated copy-buffer
unsigned char rt_copy_buffer_1[RISCV_PAGE_SIZE];
unsigned char rt_copy_buffer_2[RISCV_PAGE_SIZE];

struct exception_table_entry {
  uintptr_t fault_pc;
  uintptr_t fixup_pc;
};

extern const struct exception_table_entry __ex_table_start[];
extern const struct exception_table_entry __ex_table_end[];

static int rt_uaccess_fixup(struct encl_ctx* ctx)
{
  const struct exception_table_entry* entry;

  for (entry = __ex_table_start; entry < __ex_table_end; entry++) {
    if (entry->fault_pc == ctx->regs.sepc) {
      ctx->regs.sepc = entry->fixup_pc;
      return 1;
    }
  }
  return 0;
}

size_t rt_util_getrandom(void* vaddr, size_t buflen){
  size_t remaining = buflen;
  uintptr_t rnd;
  unsigned char* next = (unsigned char*)vaddr;

  while (remaining > 0) {
    size_t chunk = remaining > sizeof(rnd) ? sizeof(rnd) : remaining;
    rnd = sbi_random();
    if (copy_to_user(next, &rnd, chunk) != 0) {
      rnd = 0;
      return (size_t)-1;
    }
    remaining -= chunk;
    next += chunk;
  }
  rnd = 0;
  return buflen;
}

void rt_util_misc_fatal(){
  //Better hope we can debug it!
  sbi_exit_enclave(-1);
}

void not_implemented_fatal(struct encl_ctx* ctx){
#ifdef FATAL_DEBUG
    unsigned long addr, cause, pc;
    pc = ctx->regs.sepc;
    addr = ctx->sbadaddr;
    cause = ctx->scause;
    printf("[runtime] non-handlable interrupt/exception at 0x%lx on 0x%lx (scause: 0x%lx)\r\n", pc, addr, cause);
#endif

    // Bail to m-mode
    __asm__ volatile("csrr a0, scause\r\nli a7, 1111\r\n ecall");

    return;
}

void rt_page_fault(struct encl_ctx* ctx)
{
  if (rt_uaccess_fixup(ctx))
    return;

#ifdef FATAL_DEBUG
  unsigned long addr, cause, pc;
  pc = ctx->regs.sepc;
  addr = ctx->sbadaddr;
  cause = ctx->scause;
  printf("[runtime] page fault at 0x%lx on 0x%lx (scause: 0x%lx)\r\n", pc, addr, cause);
#endif

  sbi_exit_enclave(-1);

  /* never reach here */
  assert(false);
  return;
}

void tlb_flush(void)
{
  __asm__ volatile("fence.i\t\nsfence.vma\t\n");
}
