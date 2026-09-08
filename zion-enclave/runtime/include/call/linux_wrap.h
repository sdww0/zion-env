#ifdef USE_LINUX_SYSCALL
#ifndef _LINUX_WRAP_H_
#define _LINUX_WRAP_H_

#include <bits/types/sigset_t.h>
#include <stddef.h>
#include <stdint.h>

struct timespec;
struct timeval;
struct timezone;

/* RV64 Linux kernel ABI.  This intentionally does not use libc's
 * struct sigaction, whose field order and sigset_t size are different. */
struct eyrie_kernel_sigaction {
  uintptr_t handler;
  uintptr_t flags;
  uint64_t mask;
};

uintptr_t linux_uname(void* buf);
uintptr_t linux_clock_gettime(__clockid_t clock, struct timespec *tp);
uintptr_t linux_gettimeofday(struct timeval *tv, struct timezone *tz);
uintptr_t linux_rt_sigprocmask(int how, const sigset_t *set, sigset_t *oldset,
                               size_t sigsetsize);
uintptr_t linux_rt_sigaction(int signal,
                             const struct eyrie_kernel_sigaction *act,
                             struct eyrie_kernel_sigaction *oldact,
                             size_t sigsetsize);
uintptr_t linux_getrandom(void *buf, size_t buflen, unsigned int flags);
uintptr_t linux_getpid(void);
uintptr_t linux_gettid(void);
uintptr_t linux_set_tid_address(int *tidptr);
uintptr_t linux_RET_ZERO_wrap(unsigned long which);
uintptr_t linux_RET_BAD_wrap(unsigned long which);
uintptr_t syscall_munmap(void *addr, size_t length);
uintptr_t syscall_mmap(void *addr, size_t length, int prot, int flags,
                       int fd, uintptr_t offset);
uintptr_t syscall_mprotect(void *addr, size_t len, int prot);
uintptr_t syscall_brk(void* addr);
#endif /* _LINUX_WRAP_H_ */
#endif /* USE_LINUX_SYSCALL */
