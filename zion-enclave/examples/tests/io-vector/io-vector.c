#include <stddef.h>
#include <stdint.h>

#include "app/eapp_utils.h"

#define SYS_CLOSE 57
#define SYS_PIPE2 59
#define SYS_READV 65
#define SYS_WRITEV 66

#define EYRIE_IOV_MAX 1024
#define TEST_SUCCESS 13579UL

struct test_iovec {
  void *iov_base;
  size_t iov_len;
};

static uintptr_t syscall3(uintptr_t number, uintptr_t arg0, uintptr_t arg1,
                          uintptr_t arg2)
{
  register uintptr_t a0 __asm__("a0") = arg0;
  register uintptr_t a1 __asm__("a1") = arg1;
  register uintptr_t a2 __asm__("a2") = arg2;
  register uintptr_t a7 __asm__("a7") = number;

  __asm__ volatile("ecall"
                   : "+r"(a0)
                   : "r"(a1), "r"(a2), "r"(a7)
                   : "memory");
  return a0;
}

void EAPP_ENTRY eapp_entry(void)
{
  static const char first[] = "abc";
  static const char second[] = "DEF";
  struct test_iovec output[3] = {
      {(void *)first, sizeof(first) - 1},
      {NULL, 0},
      {(void *)second, sizeof(second) - 1},
  };
  struct test_iovec overflow[2] = {
      {(void *)first, INTPTR_MAX},
      {(void *)second, 1},
  };
  char left[2] = {0};
  char right[4] = {0};
  struct test_iovec input[2] = {
      {left, sizeof(left)},
      {right, sizeof(right)},
  };
  int fds[2] = {-1, -1};

  if (syscall3(SYS_WRITEV, 1, (uintptr_t)output, (uintptr_t)-1) !=
          (uintptr_t)-1 ||
      syscall3(SYS_WRITEV, 1, (uintptr_t)output, EYRIE_IOV_MAX + 1) !=
          (uintptr_t)-1 ||
      syscall3(SYS_WRITEV, 1, UINTPTR_MAX - 7, 1) != (uintptr_t)-1 ||
      syscall3(SYS_WRITEV, 1, (uintptr_t)overflow, 2) != (uintptr_t)-1)
    EAPP_RETURN(1);

  if (syscall3(SYS_PIPE2, (uintptr_t)fds, 0, 0) != 0 ||
      fds[0] < 0 || fds[1] < 0)
    EAPP_RETURN(2);

  if (syscall3(SYS_WRITEV, (uintptr_t)fds[1], (uintptr_t)output, 3) != 6)
    EAPP_RETURN(3);
  if (syscall3(SYS_READV, (uintptr_t)fds[0], (uintptr_t)input, 2) != 6)
    EAPP_RETURN(4);

  if (left[0] != 'a' || left[1] != 'b' || right[0] != 'c' ||
      right[1] != 'D' || right[2] != 'E' || right[3] != 'F')
    EAPP_RETURN(5);

  if ((intptr_t)syscall3(SYS_CLOSE, (uintptr_t)fds[0], 0, 0) < 0 ||
      (intptr_t)syscall3(SYS_CLOSE, (uintptr_t)fds[1], 0, 0) < 0)
    EAPP_RETURN(6);

  EAPP_RETURN(TEST_SUCCESS);
}
