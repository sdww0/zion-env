#include "mm/vm_defs.h"

OUTPUT_ARCH( "riscv" )

SECTIONS
{
  . = 0xffffffffc0000000;
  PROVIDE(rt_base = .);
  .text : {
    *(.text._start)
    *(.text.encl_trap_handler)
    *(.text)
    *(.fixup)
  }
  . = ALIGN(RISCV_PAGE_SIZE);
  .rodata :
  {
    *(.rdata)
    *(.rodata)
    . = ALIGN(8);
    PROVIDE(__ex_table_start = .);
    KEEP(*(__ex_table))
    PROVIDE(__ex_table_end = .);
  }
  .data : { *(.data) }
  .bss : { *(.bss) }
  . = ALIGN(RISCV_PAGE_SIZE);
  .kernel_stack : {
    . += 8 * RISCV_PAGE_SIZE;
    PROVIDE(kernel_stack_end = .);
  }

  _end = .;
}
