#include <stddef.h>
#include <stdint.h>

#include "app/eapp_utils.h"

#define SYS_BRK 214
#define SYS_MUNMAP 215
#define SYS_MMAP 222
#define SYS_MPROTECT 226

#define MAP_PRIVATE 0x02
#define MAP_ANONYMOUS 0x20
#define PROT_NONE 0x0
#define PROT_READ 0x1
#define PROT_WRITE 0x2
#define PROT_INVALID 0x8

#define EINVAL_VALUE 22
#define ENOMEM_VALUE 12

#define PAGE_SIZE 4096UL
#define EYRIE_ANON_START 0x0000002000000000UL
#define EYRIE_ANON_END 0x0000004000000000UL
#define EYRIE_RUNTIME_START 0xffffffffc0000000UL
#define TEST_SUCCESS 24680UL

extern char _end[];

static uintptr_t syscall6(uintptr_t number, uintptr_t arg0, uintptr_t arg1,
                          uintptr_t arg2, uintptr_t arg3, uintptr_t arg4,
                          uintptr_t arg5)
{
  register uintptr_t a0 __asm__("a0") = arg0;
  register uintptr_t a1 __asm__("a1") = arg1;
  register uintptr_t a2 __asm__("a2") = arg2;
  register uintptr_t a3 __asm__("a3") = arg3;
  register uintptr_t a4 __asm__("a4") = arg4;
  register uintptr_t a5 __asm__("a5") = arg5;
  register uintptr_t a7 __asm__("a7") = number;

  __asm__ volatile("ecall"
                   : "+r"(a0)
                   : "r"(a1), "r"(a2), "r"(a3), "r"(a4), "r"(a5),
                     "r"(a7)
                   : "memory");
  return a0;
}

static uintptr_t do_brk(uintptr_t address)
{
  return syscall6(SYS_BRK, address, 0, 0, 0, 0, 0);
}

static uintptr_t do_mmap_raw(size_t length, int prot, int flags, int fd,
                             uintptr_t offset)
{
  return syscall6(SYS_MMAP, 0, length, (uintptr_t)prot, (uintptr_t)flags,
                  (uintptr_t)fd, offset);
}

static uintptr_t do_mmap(size_t length, int prot)
{
  return do_mmap_raw(length, prot, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
}

static uintptr_t do_mprotect(uintptr_t address, size_t length, int prot)
{
  return syscall6(SYS_MPROTECT, address, length, (uintptr_t)prot, 0, 0, 0);
}

static uintptr_t do_munmap(uintptr_t address, size_t length)
{
  return syscall6(SYS_MUNMAP, address, length, 0, 0, 0, 0);
}

void EAPP_ENTRY eapp_entry(void)
{
  uintptr_t base = do_brk(0);
  uintptr_t expected_base =
      ((uintptr_t)_end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
  uintptr_t grown;
  uintptr_t mapping;
  uintptr_t replacement;
  volatile uint8_t *bytes;

  if (base != expected_base || base == 0 ||
      (base & (PAGE_SIZE - 1)) != 0)
    EAPP_RETURN(1);

  grown = base + 2 * PAGE_SIZE + 123;
  if (grown < base || do_brk(grown) != grown || do_brk(0) != grown)
    EAPP_RETURN(2);

  bytes = (volatile uint8_t *)base;
  bytes[0] = 0x11;
  bytes[PAGE_SIZE] = 0x22;
  bytes[2 * PAGE_SIZE] = 0x33;
  if (bytes[0] != 0x11 || bytes[PAGE_SIZE] != 0x22 ||
      bytes[2 * PAGE_SIZE] != 0x33)
    EAPP_RETURN(3);

  if (do_brk(base + 64) != base + 64 || do_brk(grown) != grown)
    EAPP_RETURN(4);
  if (bytes[PAGE_SIZE] != 0 || bytes[2 * PAGE_SIZE] != 0)
    EAPP_RETURN(5);
  if (do_brk(base - 1) != grown ||
      do_brk(EYRIE_ANON_END + 1) != grown || do_brk(0) != grown)
    EAPP_RETURN(6);

  mapping = do_mmap(PAGE_SIZE + 1, PROT_READ | PROT_WRITE);
  if (mapping == (uintptr_t)-1 || mapping < EYRIE_ANON_START ||
      mapping >= EYRIE_ANON_END || (mapping & (PAGE_SIZE - 1)) != 0)
    EAPP_RETURN(7);
  bytes = (volatile uint8_t *)mapping;
  if (bytes[0] != 0 || bytes[PAGE_SIZE] != 0)
    EAPP_RETURN(8);
  bytes[0] = 0x44;
  bytes[PAGE_SIZE] = 0x55;

  /* PROT_WRITE must remain a valid readable RISC-V leaf mapping. */
  if (do_mprotect(mapping, PAGE_SIZE + 1, PROT_WRITE) != 0)
    EAPP_RETURN(9);
  bytes[0] = 0x66;
  if (bytes[0] != 0x66)
    EAPP_RETURN(10);

  if (do_mprotect(EYRIE_RUNTIME_START, PAGE_SIZE,
                  PROT_READ | PROT_WRITE) != (uintptr_t)-ENOMEM_VALUE ||
      do_mprotect(mapping, PAGE_SIZE, PROT_NONE) != 0 ||
      do_mprotect(mapping, PAGE_SIZE, PROT_READ | PROT_WRITE) != 0 ||
      bytes[0] != 0x66 ||
      do_mprotect(mapping, PAGE_SIZE, PROT_INVALID) !=
          (uintptr_t)-EINVAL_VALUE ||
      do_mprotect(mapping + 1, PAGE_SIZE, PROT_READ) !=
          (uintptr_t)-EINVAL_VALUE ||
      do_mprotect(mapping, 0, PROT_NONE) != 0)
    EAPP_RETURN(11);
  if (do_munmap(mapping + 1, PAGE_SIZE) != (uintptr_t)-EINVAL_VALUE ||
      do_munmap(EYRIE_RUNTIME_START, PAGE_SIZE) !=
          (uintptr_t)-EINVAL_VALUE ||
      do_munmap(base, PAGE_SIZE) != (uintptr_t)-EINVAL_VALUE ||
      do_munmap((uintptr_t)eapp_entry & ~(PAGE_SIZE - 1), PAGE_SIZE) !=
          (uintptr_t)-EINVAL_VALUE ||
      do_munmap(mapping, 0) != (uintptr_t)-EINVAL_VALUE)
    EAPP_RETURN(12);

  if (do_munmap(mapping, PAGE_SIZE + 1) != 0 ||
      do_mprotect(mapping, PAGE_SIZE, PROT_READ) !=
          (uintptr_t)-ENOMEM_VALUE)
    EAPP_RETURN(13);
  replacement = do_mmap(PAGE_SIZE + 1, PROT_READ | PROT_WRITE);
  if (replacement != mapping)
    EAPP_RETURN(14);
  bytes = (volatile uint8_t *)replacement;
  if (bytes[0] != 0 || bytes[PAGE_SIZE] != 0)
    EAPP_RETURN(15);
  if (do_munmap(replacement, PAGE_SIZE + 1) != 0)
    EAPP_RETURN(16);

  if (do_mmap(0, PROT_READ | PROT_WRITE) != (uintptr_t)-EINVAL_VALUE ||
      do_mmap(SIZE_MAX, PROT_READ | PROT_WRITE) !=
          (uintptr_t)-ENOMEM_VALUE ||
      do_mmap(PAGE_SIZE, PROT_INVALID) != (uintptr_t)-EINVAL_VALUE ||
      do_mmap_raw(PAGE_SIZE, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS,
                  -1, PAGE_SIZE + 1) != (uintptr_t)-EINVAL_VALUE ||
      do_mmap_raw(PAGE_SIZE, PROT_READ, MAP_PRIVATE, -1, 0) !=
          (uintptr_t)-EINVAL_VALUE)
    EAPP_RETURN(17);

  mapping = do_mmap(PAGE_SIZE, PROT_NONE);
  if (mapping < EYRIE_ANON_START || mapping >= EYRIE_ANON_END ||
      do_mprotect(mapping, PAGE_SIZE, PROT_READ | PROT_WRITE) != 0 ||
      *(volatile uint8_t *)mapping != 0 ||
      do_munmap(mapping, PAGE_SIZE) != 0)
    EAPP_RETURN(21);

  /* Linux ignores fd and a page-aligned offset for anonymous mappings. */
  mapping = do_mmap_raw(PAGE_SIZE, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS, 123, PAGE_SIZE);
  if (mapping < EYRIE_ANON_START || mapping >= EYRIE_ANON_END ||
      do_munmap(mapping, PAGE_SIZE) != 0)
    EAPP_RETURN(22);

  /* With the runner's 512 KiB free-memory request this consumes all data
   * frames before accounting for new page-table pages. The allocation must
   * roll back completely, leaving a subsequent small mapping usable. */
  mapping = do_mmap(150 * PAGE_SIZE, PROT_READ | PROT_WRITE);
  if (mapping != (uintptr_t)-ENOMEM_VALUE) {
    do_munmap(mapping, 150 * PAGE_SIZE);
    EAPP_RETURN(18);
  }
  replacement = do_mmap(PAGE_SIZE, PROT_READ | PROT_WRITE);
  if (replacement < EYRIE_ANON_START || replacement >= EYRIE_ANON_END ||
      do_munmap(replacement, PAGE_SIZE) != 0)
    EAPP_RETURN(19);

  if (do_brk(base) != base || do_brk(0) != base)
    EAPP_RETURN(20);
  EAPP_RETURN(TEST_SUCCESS);
}
