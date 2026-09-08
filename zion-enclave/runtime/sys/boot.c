#include <asm/csr.h>

#include "util/printf.h"
#include "sys/interrupt.h"
#include "call/syscall.h"
#include "mm/vm.h"
#include "util/string.h"
#include "call/sbi.h"
#include "mm/freemem.h"
#include "mm/mm.h"
#include "sys/env.h"
#include "mm/paging.h"
#include "loader/elf.h"
#include "loader/layout.h"
#include "loader/loader.h"

/* defined in vm.h */
extern uintptr_t shared_buffer;
extern uintptr_t shared_buffer_size;

/* initial memory layout */
uintptr_t utm_base;
size_t utm_size;

/* defined in entry.S */
extern void* encl_trap_handler;

int verify_and_load_elf_file(uintptr_t ptr, size_t file_size, bool is_eapp) {
  int ret = 0;
  uintptr_t initial_program_break = 0;
  // validate elf 
  if (((void*) ptr == NULL) || (file_size <= 0)) {
    return -1; 
  }
  
  // create elf struct
  elf_t elf_file;
  ret = elf_newFile((void*) ptr, file_size, &elf_file);
  if (ret < 0) {
    return ret;
  }

  if (is_eapp &&
      eyrie_elf_user_layout(&elf_file, &initial_program_break) != 0)
    return -1;

  // parse and load elf file
  ret = loadElf(&elf_file, 1);

  if (is_eapp) { // setup entry point
    uintptr_t entry = elf_getEntryPoint(&elf_file);
    csr_write(sepc, entry);
    set_program_break(initial_program_break);
  }
  return ret;
}

/* initialize free memory with a simple page allocator*/
void
init_freemem()
{
  spa_init(freemem_va_start, freemem_size);
}

/* initialize user stack */
void
init_user_stack_and_env(ELF(Ehdr) *hdr)
{
  void* user_sp = (void*) EYRIE_USER_STACK_START;
  size_t count;
  uintptr_t stack_end = EYRIE_USER_STACK_END;
  size_t stack_count = EYRIE_USER_STACK_SIZE >> RISCV_PAGE_BITS;

  // allocated stack pages right below the runtime
  // +1 page: sp starts at EYRIE_USER_STACK_START which is one page above the
  // stack region; glibc _start reads directly from sp before any push
  count = alloc_pages(vpn(stack_end), stack_count + 1,
      PTE_R | PTE_W | PTE_D | PTE_A | PTE_U);

  assert(count == stack_count + 1);

  // setup user stack env/aux
  user_sp = setup_start(user_sp, hdr);

  // prepare user sp
  csr_write(sscratch, user_sp);
}

void
eyrie_boot(uintptr_t dummy, // $a0 contains the return value from the SBI
           uintptr_t dram_base,
           uintptr_t dram_size,
           uintptr_t runtime_paddr,
           uintptr_t user_paddr,
           uintptr_t free_paddr,
           uintptr_t utm_vaddr,
           uintptr_t utm_size)
{
  /* set initial values */
  load_pa_start = dram_base;
  root_page_table = (pte*) __va(csr_read(satp) << RISCV_PAGE_BITS);
  shared_buffer = EYRIE_UNTRUSTED_START;
  shared_buffer_size = utm_size;
  runtime_va_start = (uintptr_t) &rt_base;
  kernel_offset = runtime_va_start - runtime_paddr;

  debug("ROOT PAGE TABLE: 0x%lx", root_page_table);
  debug("UTM : 0x%lx-0x%lx (%u KB)", utm_vaddr, utm_vaddr+utm_size, utm_size/1024);
  debug("DRAM: 0x%lx-0x%lx (%u KB)", dram_base, dram_base + dram_size, dram_size/1024);
  debug("USER: 0x%lx-0x%lx (%u KB)", user_paddr, free_paddr, (free_paddr-user_paddr)/1024);

  /* set trap vector */
  csr_write(stvec, &encl_trap_handler);
  freemem_va_start = __va(free_paddr);
  freemem_size = dram_base + dram_size - free_paddr;

  /* Do not make enclave correctness depend on a polling UART in M-mode.
   * Normal application stdout is forwarded through the UTM edge-call path
   * after init_edge_internals().  These early diagnostics are debug-only
   * because the shared channel is not ready yet. */
  debug("FREE: 0x%lx-0x%lx (%u KB), va 0x%lx", free_paddr,
        dram_base + dram_size, freemem_size/1024, freemem_va_start);
  debug("kernel_offset=0x%lx, satp=0x%lx", kernel_offset, csr_read(satp));

  /* initialize free memory */
  init_freemem();

  /* load eapp elf */
  assert(!verify_and_load_elf_file(__va(user_paddr), free_paddr-user_paddr, true));

  /* free leaking memory */
  // TODO: clean up after loader -- entire file no longer needed
  // TODO: load elf file doesn't map some pages; those can be re-used. runtime and eapp.

  #ifdef USE_PAGING
  init_paging(user_paddr, free_paddr);
  #endif /* USE_PAGING */

  /* initialize user stack */
  init_user_stack_and_env((ELF(Ehdr) *) __va(user_paddr));

  /* prepare edge & system calls */
  init_edge_internals();

  /* set timer */
  init_timer();

  /* Enable the FPU */
  csr_write(sstatus, csr_read(sstatus) | 0x6000);

  debug("eyrie boot finished. drop to the user land ...");
  /* booting all finished, droping to the user land */
  return;
}
