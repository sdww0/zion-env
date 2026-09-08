#include <stddef.h>
#include <stdint.h>
#include <sys/utsname.h>
#include <time.h>

#include "app/eapp_utils.h"

#define SYS_UNAME 160
#define SYS_GETPID 172
#define SYS_GETTID 178
#define SYS_CLOCK_GETTIME 113
#define SYS_SET_TID_ADDRESS 96
#define SYS_IOCTL 29
#define SYS_RT_SIGACTION 134
#define SYS_RT_SIGPROCMASK 135
#define SYS_GETRANDOM 278

#define CLOCK_REALTIME_ID 0
#define CLOCK_MONOTONIC_ID 1
#define CLOCK_MONOTONIC_RAW_ID 4
#define SIG_BLOCK_OP 0
#define SIG_UNBLOCK_OP 1
#define SIG_SETMASK_OP 2
#define SIGKILL_VALUE 9
#define SIGSTOP_VALUE 19
#define TEST_SIGNAL 10
#define SA_NOCLDSTOP_VALUE UINT64_C(0x00000001)
#define SA_SIGINFO_VALUE UINT64_C(0x00000004)
#define SA_UNSUPPORTED_VALUE UINT64_C(0x00000400)
#define SA_EXPOSE_TAGBITS_VALUE UINT64_C(0x00000800)
#define SA_RESTART_VALUE UINT64_C(0x10000000)
#define SA_UNKNOWN_VALUE UINT64_C(0x01000000)
#define EFAULT_VALUE 14
#define EINVAL_VALUE 22
#define ENOSYS_VALUE 38
#define RUNTIME_SYSCALL_UNKNOWN_VALUE 1000
#define SBI_EXT_ZION UINT64_C(0x08424b45)
#define SBI_SM_RANDOM_VALUE 3001
#define TEST_SUCCESS 11235UL

struct kernel_sigaction {
  uintptr_t handler;
  uintptr_t flags;
  uint64_t mask;
};

_Static_assert(sizeof(struct kernel_sigaction) == 24,
               "unexpected RV64 kernel sigaction layout");

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

static int string_equal(const char *left, const char *right)
{
  size_t i = 0;

  while (left[i] != '\0' && right[i] != '\0') {
    if (left[i] != right[i])
      return 0;
    ++i;
  }
  return left[i] == right[i];
}

static uint64_t timespec_ns(const struct timespec *value)
{
  return (uint64_t)value->tv_sec * UINT64_C(1000000000) +
         (uint64_t)value->tv_nsec;
}

static uintptr_t sigprocmask_call(int how, const uint64_t *set,
                                  uint64_t *oldset, size_t size)
{
  return syscall6(SYS_RT_SIGPROCMASK, (uintptr_t)how, (uintptr_t)set,
                  (uintptr_t)oldset, size, 0, 0);
}

static uintptr_t sigaction_call(int signal,
                                const struct kernel_sigaction *action,
                                struct kernel_sigaction *old_action,
                                size_t size)
{
  return syscall6(SYS_RT_SIGACTION, (uintptr_t)signal, (uintptr_t)action,
                  (uintptr_t)old_action, size, 0, 0);
}

static uintptr_t sbi_call(uintptr_t extension, uintptr_t function,
                          uintptr_t arg0)
{
  register uintptr_t a0 __asm__("a0") = arg0;
  register uintptr_t a6 __asm__("a6") = function;
  register uintptr_t a7 __asm__("a7") = extension;

  __asm__ volatile("ecall"
                   : "+r"(a0)
                   : "r"(a6), "r"(a7)
                   : "memory");
  return a0;
}

void EAPP_ENTRY eapp_entry(void)
{
  struct timespec first = {0, 0};
  struct timespec second = {0, 0};
  struct utsname names;
  unsigned char random_a[24] = {0};
  unsigned char random_b[24] = {0};
  uint64_t old_mask = UINT64_MAX;
  uint64_t requested;
  uint64_t expected;
  struct kernel_sigaction action;
  struct kernel_sigaction replacement;
  struct kernel_sigaction observed;
  volatile uint64_t delay = 0;
  int any_random = 0;
  int random_differs = 0;

  if (syscall6(SYS_CLOCK_GETTIME, CLOCK_REALTIME_ID,
               (uintptr_t)&first, 0, 0, 0, 0) !=
          (uintptr_t)-EINVAL_VALUE ||
      syscall6(SYS_CLOCK_GETTIME, CLOCK_MONOTONIC_ID,
               UINTPTR_MAX - 7, 0, 0, 0, 0) !=
          (uintptr_t)-EFAULT_VALUE ||
      syscall6(SYS_CLOCK_GETTIME, CLOCK_MONOTONIC_ID,
               (uintptr_t)&first, 0, 0, 0, 0) != 0 ||
      first.tv_sec < 0 || first.tv_nsec < 0 ||
      first.tv_nsec >= 1000000000L) {
    EAPP_RETURN(1);
  }
  for (size_t i = 0; i < 20000; ++i)
    delay += i;
  if (delay == UINT64_MAX ||
      syscall6(SYS_CLOCK_GETTIME, CLOCK_MONOTONIC_RAW_ID,
               (uintptr_t)&second, 0, 0, 0, 0) != 0 ||
      second.tv_nsec < 0 || second.tv_nsec >= 1000000000L ||
      timespec_ns(&second) <= timespec_ns(&first)) {
    EAPP_RETURN(2);
  }

  if (syscall6(SYS_GETRANDOM, 0, 0, 0, 0, 0, 0) != 0 ||
      syscall6(SYS_GETRANDOM, UINTPTR_MAX - 3, 8, 0, 0, 0, 0) !=
          (uintptr_t)-EFAULT_VALUE ||
      syscall6(SYS_GETRANDOM, (uintptr_t)random_a, sizeof(random_a),
               UINT32_C(0x80000000), 0, 0, 0) !=
          (uintptr_t)-EINVAL_VALUE ||
      syscall6(SYS_GETRANDOM, (uintptr_t)random_a, sizeof(random_a),
               0, 0, 0, 0) != sizeof(random_a) ||
      syscall6(SYS_GETRANDOM, (uintptr_t)random_b, sizeof(random_b),
               1, 0, 0, 0) != sizeof(random_b)) {
    EAPP_RETURN(3);
  }
  for (size_t i = 0; i < sizeof(random_a); ++i) {
    any_random |= random_a[i] != 0 || random_b[i] != 0;
    random_differs |= random_a[i] != random_b[i];
  }
  if (!any_random || !random_differs)
    EAPP_RETURN(4);

  for (size_t i = 0; i < sizeof(names); ++i)
    ((unsigned char *)&names)[i] = 0xa5;
  if (syscall6(SYS_UNAME, UINTPTR_MAX - 7, 0, 0, 0, 0, 0) !=
          (uintptr_t)-EFAULT_VALUE ||
      syscall6(SYS_UNAME, (uintptr_t)&names, 0, 0, 0, 0, 0) != 0 ||
      !string_equal(names.sysname, "Linux") ||
      !string_equal(names.nodename, "enclave") ||
      !string_equal(names.release, "5.16.0") ||
      !string_equal(names.version, "Eyrie") ||
      !string_equal(names.machine, "riscv64") ||
      names.sysname[sizeof(names.sysname) - 1] != '\0' ||
      names.nodename[sizeof(names.nodename) - 1] != '\0' ||
      names.release[sizeof(names.release) - 1] != '\0' ||
      names.version[sizeof(names.version) - 1] != '\0' ||
      names.machine[sizeof(names.machine) - 1] != '\0') {
    EAPP_RETURN(5);
  }

  if (sigprocmask_call(99, NULL, &old_mask, sizeof(old_mask) - 1) !=
          (uintptr_t)-EINVAL_VALUE ||
      sigprocmask_call(99, NULL, &old_mask, sizeof(old_mask)) != 0 ||
      old_mask != 0) {
    EAPP_RETURN(6);
  }

  requested = (UINT64_C(1) << 2) | (UINT64_C(1) << (9 - 1)) |
              (UINT64_C(1) << (19 - 1));
  old_mask = UINT64_MAX;
  if (sigprocmask_call(SIG_BLOCK_OP, &requested, &old_mask,
                       sizeof(old_mask)) != 0 ||
      old_mask != 0) {
    EAPP_RETURN(7);
  }
  expected = UINT64_C(1) << 2;
  old_mask = 0;
  if (sigprocmask_call(99, NULL, &old_mask, sizeof(old_mask)) != 0 ||
      old_mask != expected ||
      sigprocmask_call(99, &requested, NULL, sizeof(old_mask)) !=
          (uintptr_t)-EINVAL_VALUE ||
      sigprocmask_call(SIG_BLOCK_OP,
                       (const uint64_t *)(UINTPTR_MAX - 3), NULL,
                       sizeof(old_mask)) != (uintptr_t)-EFAULT_VALUE ||
      sigprocmask_call(99, NULL, (uint64_t *)(UINTPTR_MAX - 3),
                       sizeof(old_mask)) != (uintptr_t)-EFAULT_VALUE) {
    EAPP_RETURN(8);
  }

  requested = expected;
  if (sigprocmask_call(SIG_UNBLOCK_OP, &requested, NULL,
                       sizeof(requested)) != 0) {
    EAPP_RETURN(9);
  }
  requested = UINT64_C(1) << 3;
  if (sigprocmask_call(SIG_SETMASK_OP, &requested, NULL,
                       sizeof(requested)) != 0) {
    EAPP_RETURN(10);
  }
  old_mask = 0;
  if (sigprocmask_call(99, NULL, &old_mask, sizeof(old_mask)) != 0 ||
      old_mask != requested) {
    EAPP_RETURN(11);
  }
  requested = 0;
  if (sigprocmask_call(SIG_SETMASK_OP, &requested, NULL,
                       sizeof(requested)) != 0)
    EAPP_RETURN(12);

  observed.handler = UINTPTR_MAX;
  observed.flags = UINTPTR_MAX;
  observed.mask = UINT64_MAX;
  if (sigaction_call(TEST_SIGNAL, NULL, &observed,
                     sizeof(observed.mask) - 1) !=
          (uintptr_t)-EINVAL_VALUE ||
      sigaction_call(0, NULL, &observed, sizeof(observed.mask)) !=
          (uintptr_t)-EINVAL_VALUE ||
      sigaction_call(65, NULL, &observed, sizeof(observed.mask)) !=
          (uintptr_t)-EINVAL_VALUE ||
      sigaction_call(TEST_SIGNAL, NULL, &observed,
                     sizeof(observed.mask)) != 0 ||
      observed.handler != 0 || observed.flags != 0 || observed.mask != 0) {
    EAPP_RETURN(13);
  }

  action.handler = (uintptr_t)UINT64_C(0x123456789abcdef0);
  action.flags = SA_NOCLDSTOP_VALUE | SA_SIGINFO_VALUE |
                 SA_UNSUPPORTED_VALUE | SA_EXPOSE_TAGBITS_VALUE |
                 SA_RESTART_VALUE | SA_UNKNOWN_VALUE;
  action.mask = (UINT64_C(1) << 2) |
                (UINT64_C(1) << (SIGKILL_VALUE - 1)) |
                (UINT64_C(1) << (SIGSTOP_VALUE - 1));
  if (sigaction_call(SIGKILL_VALUE, &action, NULL,
                     sizeof(action.mask)) != (uintptr_t)-EINVAL_VALUE ||
      sigaction_call(SIGSTOP_VALUE, &action, NULL,
                     sizeof(action.mask)) != (uintptr_t)-EINVAL_VALUE ||
      sigaction_call(SIGKILL_VALUE, NULL, &observed,
                     sizeof(action.mask)) != 0 ||
      observed.handler != 0 || observed.flags != 0 || observed.mask != 0) {
    EAPP_RETURN(14);
  }

  observed.handler = UINTPTR_MAX;
  observed.flags = UINTPTR_MAX;
  observed.mask = UINT64_MAX;
  if (sigaction_call(TEST_SIGNAL, &action, &observed,
                     sizeof(action.mask)) != 0 ||
      observed.handler != 0 || observed.flags != 0 || observed.mask != 0 ||
      action.flags != (SA_NOCLDSTOP_VALUE | SA_SIGINFO_VALUE |
                       SA_UNSUPPORTED_VALUE | SA_EXPOSE_TAGBITS_VALUE |
                       SA_RESTART_VALUE | SA_UNKNOWN_VALUE) ||
      action.mask != ((UINT64_C(1) << 2) |
                      (UINT64_C(1) << (SIGKILL_VALUE - 1)) |
                      (UINT64_C(1) << (SIGSTOP_VALUE - 1)))) {
    EAPP_RETURN(15);
  }

  observed.handler = 0;
  observed.flags = 0;
  observed.mask = 0;
  expected = SA_NOCLDSTOP_VALUE | SA_SIGINFO_VALUE |
             SA_EXPOSE_TAGBITS_VALUE | SA_RESTART_VALUE;
  if (sigaction_call(TEST_SIGNAL, NULL, &observed,
                     sizeof(observed.mask)) != 0 ||
      observed.handler != action.handler || observed.flags != expected ||
      observed.mask != (UINT64_C(1) << 2)) {
    EAPP_RETURN(16);
  }

  replacement.handler = (uintptr_t)UINT64_C(0x0fedcba987654321);
  replacement.flags = 0;
  replacement.mask = UINT64_C(1) << 5;
  observed.handler = 0;
  observed.flags = 0;
  observed.mask = 0;
  if (sigaction_call(TEST_SIGNAL, &replacement, &observed,
                     sizeof(replacement.mask)) != 0 ||
      observed.handler != action.handler || observed.flags != expected ||
      observed.mask != (UINT64_C(1) << 2)) {
    EAPP_RETURN(17);
  }

  if (sigaction_call(TEST_SIGNAL,
                     (const struct kernel_sigaction *)(UINTPTR_MAX - 3),
                     NULL, sizeof(replacement.mask)) !=
          (uintptr_t)-EFAULT_VALUE ||
      sigaction_call(TEST_SIGNAL, NULL, &observed,
                     sizeof(observed.mask)) != 0 ||
      observed.handler != replacement.handler || observed.flags != 0 ||
      observed.mask != replacement.mask) {
    EAPP_RETURN(18);
  }

  action.handler = (uintptr_t)UINT64_C(0x1111222233334444);
  action.flags = SA_RESTART_VALUE;
  action.mask = UINT64_C(1) << 6;
  if (sigaction_call(TEST_SIGNAL, &action,
                     (struct kernel_sigaction *)(UINTPTR_MAX - 3),
                     sizeof(action.mask)) != (uintptr_t)-EFAULT_VALUE ||
      sigaction_call(TEST_SIGNAL, NULL, &observed,
                     sizeof(observed.mask)) != 0 ||
      observed.handler != action.handler ||
      observed.flags != SA_RESTART_VALUE || observed.mask != action.mask ||
      sigaction_call(TEST_SIGNAL, NULL,
                     (struct kernel_sigaction *)(UINTPTR_MAX - 3),
                     sizeof(action.mask)) != (uintptr_t)-EFAULT_VALUE) {
    EAPP_RETURN(19);
  }

  {
    int clear_tid = 0x12345678;
    uintptr_t pid = syscall6(SYS_GETPID, 0, 0, 0, 0, 0, 0);
    uintptr_t tid = syscall6(SYS_GETTID, 0, 0, 0, 0, 0, 0);

    if (pid == 0 || tid != pid ||
        syscall6(SYS_SET_TID_ADDRESS, (uintptr_t)&clear_tid,
                 0, 0, 0, 0, 0) != tid ||
        clear_tid != 0x12345678 ||
        syscall6(SYS_SET_TID_ADDRESS, UINTPTR_MAX - 3,
                 0, 0, 0, 0, 0) != tid ||
        syscall6(SYS_SET_TID_ADDRESS, 0, 0, 0, 0, 0, 0) != tid) {
      EAPP_RETURN(20);
    }
  }

  if (syscall6(SYS_IOCTL, 0, 0, 0, 0, 0, 0) !=
          (uintptr_t)-ENOSYS_VALUE ||
      syscall6(999, 0, 0, 0, 0, 0, 0) != (uintptr_t)-ENOSYS_VALUE ||
      syscall6(RUNTIME_SYSCALL_UNKNOWN_VALUE, 0, 0, 0, 0, 0, 0) !=
          (uintptr_t)-ENOSYS_VALUE ||
      sbi_call(UINT64_C(0x0badc0de), 0, 0) !=
          (uintptr_t)-ENOSYS_VALUE ||
      sbi_call(SBI_EXT_ZION, SBI_SM_RANDOM_VALUE, 0) !=
          (uintptr_t)-ENOSYS_VALUE) {
    EAPP_RETURN(21);
  }

  EAPP_RETURN(TEST_SUCCESS);
}
