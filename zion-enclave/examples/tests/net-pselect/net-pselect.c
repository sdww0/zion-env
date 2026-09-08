#include <stddef.h>
#include <stdint.h>
#include <sys/select.h>
#include <time.h>

#include "app/eapp_utils.h"

#define SYS_CLOSE 57
#define SYS_PIPE2 59
#define SYS_WRITE 64
#define SYS_PSELECT6 72

#define EBADF 9
#define TEST_SUCCESS 16180UL

struct pselect6_sigmask {
  const uint64_t *sigmask;
  size_t sigsetsize;
};

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

static void close_if_open(int fd)
{
  if (fd >= 0)
    syscall6(SYS_CLOSE, (uintptr_t)fd, 0, 0, 0, 0, 0);
}

static void cleanup(int readfd, int writefd)
{
  close_if_open(readfd);
  close_if_open(writefd);
}

void EAPP_ENTRY eapp_entry(void)
{
  static const unsigned char byte = 0xa5;
  uint64_t empty_mask = 0;
  struct pselect6_sigmask mask_arg = {&empty_mask, sizeof(empty_mask)};
  struct pselect6_sigmask short_mask = {&empty_mask, sizeof(empty_mask) - 1};
  struct pselect6_sigmask bad_mask = {
      (const uint64_t *)(UINTPTR_MAX - 3), sizeof(empty_mask)};
  struct timespec timeout = {0, 0};
  fd_set readfds;
  int fds[2] = {-1, -1};
  uintptr_t result;

  if (syscall6(SYS_PIPE2, (uintptr_t)fds, 0, 0, 0, 0, 0) != 0 ||
      fds[0] < 0 || fds[1] < 0) {
    cleanup(fds[0], fds[1]);
    EAPP_RETURN(1);
  }

  FD_ZERO(&readfds);
  FD_SET(fds[0], &readfds);
  if (syscall6(SYS_PSELECT6, FD_SETSIZE + 1, (uintptr_t)&readfds,
               0, 0, (uintptr_t)&timeout, 0) != (uintptr_t)-1 ||
      syscall6(SYS_PSELECT6, (uintptr_t)fds[0] + 1, UINTPTR_MAX - 3,
               0, 0, (uintptr_t)&timeout, 0) != (uintptr_t)-1 ||
      syscall6(SYS_PSELECT6, (uintptr_t)fds[0] + 1,
               (uintptr_t)&readfds, 0, 0, UINTPTR_MAX - 3, 0) !=
          (uintptr_t)-1) {
    cleanup(fds[0], fds[1]);
    EAPP_RETURN(2);
  }

  if (syscall6(SYS_PSELECT6, (uintptr_t)fds[0] + 1,
               (uintptr_t)&readfds, 0, 0, (uintptr_t)&timeout,
               UINTPTR_MAX - 7) != (uintptr_t)-1 ||
      syscall6(SYS_PSELECT6, (uintptr_t)fds[0] + 1,
               (uintptr_t)&readfds, 0, 0, (uintptr_t)&timeout,
               (uintptr_t)&short_mask) != (uintptr_t)-1 ||
      syscall6(SYS_PSELECT6, (uintptr_t)fds[0] + 1,
               (uintptr_t)&readfds, 0, 0, (uintptr_t)&timeout,
               (uintptr_t)&bad_mask) != (uintptr_t)-1) {
    cleanup(fds[0], fds[1]);
    EAPP_RETURN(3);
  }

  FD_ZERO(&readfds);
  FD_SET(fds[0], &readfds);
  timeout.tv_sec = 0;
  timeout.tv_nsec = 0;
  result = syscall6(SYS_PSELECT6, (uintptr_t)fds[0] + 1,
                    (uintptr_t)&readfds, 0, 0, (uintptr_t)&timeout,
                    (uintptr_t)&mask_arg);
  if (result != 0 || FD_ISSET(fds[0], &readfds)) {
    cleanup(fds[0], fds[1]);
    EAPP_RETURN(4);
  }

  if (syscall6(SYS_WRITE, (uintptr_t)fds[1], (uintptr_t)&byte,
               sizeof(byte), 0, 0, 0) != sizeof(byte)) {
    cleanup(fds[0], fds[1]);
    EAPP_RETURN(5);
  }

  FD_ZERO(&readfds);
  FD_SET(fds[0], &readfds);
  timeout.tv_sec = 0;
  timeout.tv_nsec = 0;
  result = syscall6(SYS_PSELECT6, (uintptr_t)fds[0] + 1,
                    (uintptr_t)&readfds, 0, 0, (uintptr_t)&timeout,
                    (uintptr_t)&mask_arg);
  if (result != 1 || !FD_ISSET(fds[0], &readfds)) {
    cleanup(fds[0], fds[1]);
    EAPP_RETURN(6);
  }

  if ((intptr_t)syscall6(SYS_CLOSE, (uintptr_t)fds[0], 0, 0, 0, 0, 0) < 0) {
    cleanup(-1, fds[1]);
    EAPP_RETURN(7);
  }
  FD_ZERO(&readfds);
  FD_SET(fds[0], &readfds);
  timeout.tv_sec = 0;
  timeout.tv_nsec = 0;
  if (syscall6(SYS_PSELECT6, (uintptr_t)fds[0] + 1,
               (uintptr_t)&readfds, 0, 0, (uintptr_t)&timeout, 0) !=
      (uintptr_t)-EBADF) {
    cleanup(-1, fds[1]);
    EAPP_RETURN(8);
  }
  fds[0] = -1;

  if ((intptr_t)syscall6(SYS_CLOSE, (uintptr_t)fds[1], 0, 0, 0, 0, 0) < 0)
    EAPP_RETURN(9);

  EAPP_RETURN(TEST_SUCCESS);
}
