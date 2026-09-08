#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/epoll.h>

#include "app/eapp_utils.h"

#define SYS_EPOLL_CREATE1 20
#define SYS_EPOLL_CTL 21
#define SYS_EPOLL_PWAIT 22
#define SYS_FCNTL 25
#define SYS_CLOSE 57
#define SYS_PIPE2 59
#define SYS_WRITE 64

#define TEST_SUCCESS 27182UL

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

static void cleanup(int epfd, int readfd, int writefd)
{
  close_if_open(epfd);
  close_if_open(readfd);
  close_if_open(writefd);
}

void EAPP_ENTRY eapp_entry(void)
{
  static const unsigned char byte = 0x5a;
  const uint64_t token = UINT64_C(0x1122334455667788);
  struct epoll_event watched = {0};
  struct epoll_event ready = {0};
  int fds[2] = {-1, -1};
  int epfd = -1;
  intptr_t result;
  int flags;

  if (syscall6(SYS_PIPE2, UINTPTR_MAX - 3, 0, 0, 0, 0, 0) !=
          (uintptr_t)-1 ||
      syscall6(SYS_PIPE2, (uintptr_t)fds, UINTPTR_MAX, 0, 0, 0, 0) !=
          (uintptr_t)-EINVAL) {
    cleanup(epfd, fds[0], fds[1]);
    EAPP_RETURN(1);
  }

  if (syscall6(SYS_PIPE2, (uintptr_t)fds, O_NONBLOCK | O_CLOEXEC,
               0, 0, 0, 0) != 0 ||
      fds[0] < 0 || fds[1] < 0) {
    cleanup(epfd, fds[0], fds[1]);
    EAPP_RETURN(2);
  }

  flags = (int)syscall6(SYS_FCNTL, (uintptr_t)fds[0], F_GETFL, 0, 0, 0, 0);
  result = (intptr_t)syscall6(SYS_FCNTL, (uintptr_t)fds[0], F_GETFD,
                              0, 0, 0, 0);
  if (flags < 0 || (flags & O_NONBLOCK) == 0 || result < 0 ||
      (result & FD_CLOEXEC) == 0) {
    cleanup(epfd, fds[0], fds[1]);
    EAPP_RETURN(3);
  }
  result = (intptr_t)syscall6(SYS_FCNTL, (uintptr_t)fds[1], F_GETFD,
                              0, 0, 0, 0);
  if (result < 0 || (result & FD_CLOEXEC) == 0) {
    cleanup(epfd, fds[0], fds[1]);
    EAPP_RETURN(3);
  }

  if (syscall6(SYS_FCNTL, (uintptr_t)fds[0], F_GETOWN_EX,
               UINTPTR_MAX - 7, 0, 0, 0) != (uintptr_t)-1 ||
      syscall6(SYS_FCNTL, (uintptr_t)fds[0], F_GETLK,
               UINTPTR_MAX - 7, 0, 0, 0) != (uintptr_t)-1) {
    cleanup(epfd, fds[0], fds[1]);
    EAPP_RETURN(4);
  }

  if (syscall6(SYS_EPOLL_CREATE1, UINTPTR_MAX, 0, 0, 0, 0, 0) !=
      (uintptr_t)-EINVAL) {
    cleanup(epfd, fds[0], fds[1]);
    EAPP_RETURN(5);
  }
  epfd = (int)syscall6(SYS_EPOLL_CREATE1, EPOLL_CLOEXEC, 0, 0, 0, 0, 0);
  result = epfd < 0
               ? -1
               : (intptr_t)syscall6(SYS_FCNTL, (uintptr_t)epfd, F_GETFD,
                                    0, 0, 0, 0);
  if (epfd < 0 || result < 0 || (result & FD_CLOEXEC) == 0) {
    cleanup(epfd, fds[0], fds[1]);
    EAPP_RETURN(6);
  }

  watched.events = EPOLLIN;
  watched.data.u64 = token;
  if (syscall6(SYS_EPOLL_CTL, (uintptr_t)epfd, EPOLL_CTL_ADD,
               (uintptr_t)fds[0], 0, 0, 0) != (uintptr_t)-1 ||
      syscall6(SYS_EPOLL_CTL, (uintptr_t)epfd, EPOLL_CTL_ADD,
               (uintptr_t)fds[0], UINTPTR_MAX - 3, 0, 0) != (uintptr_t)-1) {
    cleanup(epfd, fds[0], fds[1]);
    EAPP_RETURN(7);
  }

  if (syscall6(SYS_EPOLL_CTL, (uintptr_t)epfd, EPOLL_CTL_ADD,
               (uintptr_t)fds[0], (uintptr_t)&watched, 0, 0) != 0) {
    cleanup(epfd, fds[0], fds[1]);
    EAPP_RETURN(8);
  }

  if (syscall6(SYS_WRITE, (uintptr_t)fds[1], (uintptr_t)&byte,
               sizeof(byte), 0, 0, 0) != sizeof(byte)) {
    cleanup(epfd, fds[0], fds[1]);
    EAPP_RETURN(9);
  }

  if (syscall6(SYS_EPOLL_PWAIT, (uintptr_t)epfd, (uintptr_t)&ready, 2,
               0, 0, 0) != (uintptr_t)-1 ||
      syscall6(SYS_EPOLL_PWAIT, (uintptr_t)epfd, (uintptr_t)&ready, 1,
               0, (uintptr_t)&token, sizeof(token)) != (uintptr_t)-1 ||
      syscall6(SYS_EPOLL_PWAIT, (uintptr_t)epfd, UINTPTR_MAX - 3, 1,
               0, 0, 0) != (uintptr_t)-1) {
    cleanup(epfd, fds[0], fds[1]);
    EAPP_RETURN(10);
  }

  result = (intptr_t)syscall6(SYS_EPOLL_PWAIT, (uintptr_t)epfd,
                              (uintptr_t)&ready, 1, 0, 0, 0);
  if (result != 1 || (ready.events & EPOLLIN) == 0 ||
      ready.data.u64 != token) {
    cleanup(epfd, fds[0], fds[1]);
    EAPP_RETURN(11);
  }

  if (syscall6(SYS_EPOLL_CTL, (uintptr_t)epfd, EPOLL_CTL_DEL,
               (uintptr_t)fds[0], 0, 0, 0) != 0 ||
      syscall6(SYS_EPOLL_PWAIT, (uintptr_t)epfd, (uintptr_t)&ready, 1,
               0, 0, 0) != 0) {
    cleanup(epfd, fds[0], fds[1]);
    EAPP_RETURN(12);
  }

  if ((intptr_t)syscall6(SYS_CLOSE, (uintptr_t)epfd, 0, 0, 0, 0, 0) < 0 ||
      (intptr_t)syscall6(SYS_CLOSE, (uintptr_t)fds[0], 0, 0, 0, 0, 0) < 0 ||
      (intptr_t)syscall6(SYS_CLOSE, (uintptr_t)fds[1], 0, 0, 0, 0, 0) < 0)
    EAPP_RETURN(13);

  EAPP_RETURN(TEST_SUCCESS);
}
