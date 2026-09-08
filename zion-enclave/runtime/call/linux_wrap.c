#ifdef USE_LINUX_SYSCALL

#define _GNU_SOURCE
#include "call/linux_wrap.h"

#include <errno.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/random.h>
#include <sys/time.h>
#include <sys/utsname.h>
#include <time.h>

#include "mm/freemem.h"
#include "mm/mm.h"
#include "util/rt_util.h"
#include "util/string.h"
#include "call/sbi.h"
#include "call/syscall.h"
#include "uaccess.h"

#define NSEC_PER_SEC UINT64_C(1000000000)
#define USEC_PER_SEC UINT64_C(1000000)
#define EYRIE_SIGNAL_COUNT 64
#define EYRIE_SINGLE_THREAD_ID 2
#define EYRIE_SA_SUPPORTED                                                \
  ((uintptr_t)UINT64_C(0x00000001) | (uintptr_t)UINT64_C(0x00000002) |   \
   (uintptr_t)UINT64_C(0x00000004) | (uintptr_t)UINT64_C(0x00000800) |   \
   (uintptr_t)UINT64_C(0x08000000) | (uintptr_t)UINT64_C(0x10000000) |   \
   (uintptr_t)UINT64_C(0x40000000) | (uintptr_t)UINT64_C(0x80000000))

static uint64_t linux_signal_mask;
static struct eyrie_kernel_sigaction linux_signal_actions[EYRIE_SIGNAL_COUNT];
static int *linux_clear_child_tid;
static uint64_t linux_timebase_frequency;

_Static_assert(sizeof(struct eyrie_kernel_sigaction) == 3 * sizeof(uint64_t),
               "unexpected RV64 kernel sigaction layout");

static int page_up_safe(uintptr_t value, uintptr_t *rounded)
{
  if (!rounded || value > UINTPTR_MAX - (RISCV_PAGE_SIZE - 1))
    return -1;
  *rounded = (value + RISCV_PAGE_SIZE - 1) &
             ~((uintptr_t)RISCV_PAGE_SIZE - 1);
  return 0;
}

static int page_range(uintptr_t addr, size_t length, uintptr_t *first_vpn,
                      size_t *page_count)
{
  uintptr_t rounded_length;

  if (!first_vpn || !page_count || length == 0 ||
      !IS_ALIGNED(addr, RISCV_PAGE_BITS) ||
      page_up_safe((uintptr_t)length, &rounded_length) != 0 ||
      rounded_length == 0 || addr > UINTPTR_MAX - rounded_length)
    return -1;

  *first_vpn = vpn(addr);
  *page_count = rounded_length >> RISCV_PAGE_BITS;
  return 0;
}

static int user_leaf_range(uintptr_t first_vpn, size_t page_count)
{
  for (size_t i = 0; i < page_count; ++i) {
    pte *entry;
    if (first_vpn > UINTPTR_MAX - i)
      return -1;
    entry = pte_of_va((first_vpn + i) << RISCV_PAGE_BITS);
    if (!entry ||
        (((*entry & (PTE_V | PTE_U)) != (PTE_V | PTE_U) ||
          !(*entry & (PTE_R | PTE_W | PTE_X))) &&
         !pte_is_prot_none(*entry)))
      return -1;
  }
  return 0;
}

static uint64_t get_timebase_frequency(void)
{
  uint64_t frequency = linux_timebase_frequency;

  if (frequency == 0) {
    frequency = sbi_timebase_frequency();
    if (frequency != 0)
      linux_timebase_frequency = frequency;
  }
  return frequency;
}

uintptr_t linux_clock_gettime(__clockid_t clock, struct timespec *tp){
  uint64_t ticks;
  uint64_t frequency;
  struct timespec result;

  /* musl implements gettimeofday() through clock_gettime(CLOCK_REALTIME).
   * Eyrie has no trusted wall-clock epoch, so REALTIME is boot-relative, but
   * it remains monotonic and is valid for elapsed-time measurements. */
  if (clock != CLOCK_REALTIME && clock != CLOCK_MONOTONIC &&
      clock != CLOCK_MONOTONIC_RAW && clock != CLOCK_BOOTTIME)
    return (uintptr_t)-EINVAL;

  frequency = get_timebase_frequency();
  if (frequency == 0)
    return (uintptr_t)-EIO;

  /* rdtime, unlike rdcycle, has stable wall-duration semantics across CPU
   * frequency changes.  The monitor reports the platform frequency because
   * QEMU uses 10 MHz while Megrez uses 1 MHz. */
  __asm__ __volatile__("rdtime %0" : "=r"(ticks));
  result.tv_sec = (time_t)(ticks / frequency);
  result.tv_nsec =
      (long)(((ticks % frequency) * NSEC_PER_SEC) / frequency);

  if (copy_to_user(tp, &result, sizeof(result)) != 0)
    return (uintptr_t)-EFAULT;

  return 0;
}

uintptr_t linux_gettimeofday(struct timeval *tv, struct timezone *tz){
  uint64_t ticks;
  uint64_t frequency;
  struct timeval result;
  struct timezone zone = { 0, 0 };

  frequency = get_timebase_frequency();
  if (frequency == 0)
    return (uintptr_t)-EIO;

  __asm__ __volatile__("rdtime %0" : "=r"(ticks));
  result.tv_sec = (time_t)(ticks / frequency);
  result.tv_usec =
      (suseconds_t)(((ticks % frequency) * USEC_PER_SEC) / frequency);

  if (tv != NULL && copy_to_user(tv, &result, sizeof(result)) != 0)
    return (uintptr_t)-EFAULT;
  if (tz != NULL && copy_to_user(tz, &zone, sizeof(zone)) != 0)
    return (uintptr_t)-EFAULT;

  return 0;
}

uintptr_t linux_set_tid_address(int *tidptr){
  /* Linux records this pointer without probing it.  Eyrie currently has one
   * thread and destroys its address space when that thread exits, matching
   * Linux's last-mm-user path where there is no observable clear/futex wake. */
  linux_clear_child_tid = tidptr;
  print_strace("[runtime] set_tid_address (%p) = %d\r\n", tidptr,
               EYRIE_SINGLE_THREAD_ID);
  return EYRIE_SINGLE_THREAD_ID;
}

uintptr_t linux_rt_sigprocmask(int how, const sigset_t *set, sigset_t *oldset,
                               size_t sigsetsize){
  uint64_t requested_mask;
  uint64_t old_mask;
  const uint64_t unmaskable =
      (UINT64_C(1) << (SIGKILL - 1)) |
      (UINT64_C(1) << (SIGSTOP - 1));

  if (sigsetsize != sizeof(uint64_t))
    return (uintptr_t)-EINVAL;

  old_mask = linux_signal_mask;
  if (set != NULL) {
    if (copy_from_user(&requested_mask, set, sizeof(requested_mask)) != 0)
      return (uintptr_t)-EFAULT;
    requested_mask &= ~unmaskable;
    switch (how) {
    case SIG_BLOCK:
      linux_signal_mask |= requested_mask;
      break;
    case SIG_UNBLOCK:
      linux_signal_mask &= ~requested_mask;
      break;
    case SIG_SETMASK:
      linux_signal_mask = requested_mask;
      break;
    default:
      return (uintptr_t)-EINVAL;
    }
  }

  if (oldset != NULL &&
      copy_to_user(oldset, &old_mask, sizeof(old_mask)) != 0)
    return (uintptr_t)-EFAULT;
  return 0;
}

uintptr_t linux_rt_sigaction(int signal,
                             const struct eyrie_kernel_sigaction *act,
                             struct eyrie_kernel_sigaction *oldact,
                             size_t sigsetsize){
  struct eyrie_kernel_sigaction new_action;
  struct eyrie_kernel_sigaction old_action;
  const uint64_t unmaskable =
      (UINT64_C(1) << (SIGKILL - 1)) |
      (UINT64_C(1) << (SIGSTOP - 1));

  /* Linux checks the raw RV64 sigset size and reads act before validating the
   * signal number.  Preserve that observable error ordering. */
  if (sigsetsize != sizeof(uint64_t))
    return (uintptr_t)-EINVAL;
  if (act != NULL &&
      copy_from_user(&new_action, act, sizeof(new_action)) != 0)
    return (uintptr_t)-EFAULT;
  if (signal < 1 || signal > EYRIE_SIGNAL_COUNT ||
      (act != NULL && (signal == SIGKILL || signal == SIGSTOP)))
    return (uintptr_t)-EINVAL;

  old_action = linux_signal_actions[signal - 1];
  if (act != NULL) {
    new_action.flags &= EYRIE_SA_SUPPORTED;
    new_action.mask &= ~unmaskable;
    linux_signal_actions[signal - 1] = new_action;
  }

  /* Like Linux, an old-action copyout fault does not roll back a new action
   * that was already installed. */
  if (oldact != NULL &&
      copy_to_user(oldact, &old_action, sizeof(old_action)) != 0)
    return (uintptr_t)-EFAULT;
  return 0;
}

uintptr_t linux_RET_ZERO_wrap(unsigned long which){
  print_strace("[runtime] Cannot handle syscall %lu, IGNORING = 0\r\n", which);
  return 0;
}

uintptr_t linux_RET_BAD_wrap(unsigned long which){
  print_strace("[runtime] Cannot handle syscall %lu, FAILING = -1\r\n", which);
  return -1;
}

uintptr_t linux_getpid(void){
  print_strace("[runtime] getpid = %d\r\n", EYRIE_SINGLE_THREAD_ID);
  return EYRIE_SINGLE_THREAD_ID;
}

uintptr_t linux_gettid(void){
  print_strace("[runtime] gettid = %d\r\n", EYRIE_SINGLE_THREAD_ID);
  return EYRIE_SINGLE_THREAD_ID;
}

uintptr_t linux_getrandom(void *buf, size_t buflen, unsigned int flags){
  unsigned int supported = GRND_NONBLOCK | GRND_RANDOM;
#ifdef GRND_INSECURE
  supported |= GRND_INSECURE;
#endif

  if ((flags & ~supported) != 0)
    return (uintptr_t)-EINVAL;

  uintptr_t ret = rt_util_getrandom(buf, buflen);
  if (ret == (uintptr_t)-1)
    ret = (uintptr_t)-EFAULT;
  print_strace("[runtime] getrandom (size %lx, flags %x) = ret %lu\r\n",
               buflen, flags, ret);
  return ret;
}

#define UNAME_SYSNAME "Linux"
#define UNAME_NODENAME "enclave"
#define UNAME_RELEASE "5.16.0"
#define UNAME_VERSION "Eyrie"
#define UNAME_MACHINE "riscv64"

uintptr_t linux_uname(void* buf){
  struct utsname result = {0};

  memcpy(result.sysname, UNAME_SYSNAME, sizeof(UNAME_SYSNAME));
  memcpy(result.nodename, UNAME_NODENAME, sizeof(UNAME_NODENAME));
  memcpy(result.release, UNAME_RELEASE, sizeof(UNAME_RELEASE));
  memcpy(result.version, UNAME_VERSION, sizeof(UNAME_VERSION));
  memcpy(result.machine, UNAME_MACHINE, sizeof(UNAME_MACHINE));

  if (copy_to_user(buf, &result, sizeof(result)) != 0)
    return (uintptr_t)-EFAULT;
  print_strace("[runtime] uname = 0\n");
  return 0;
}

uintptr_t syscall_munmap(void *addr, size_t length){
  uintptr_t first_vpn;
  uintptr_t anon_start_vpn = vpn(EYRIE_ANON_REGION_START);
  uintptr_t anon_end_vpn = vpn(EYRIE_ANON_REGION_END);
  size_t page_count;

  if (page_range((uintptr_t)addr, length, &first_vpn, &page_count) != 0)
    return (uintptr_t)-EINVAL;
  if (first_vpn < anon_start_vpn || first_vpn >= anon_end_vpn ||
      page_count > anon_end_vpn - first_vpn)
    return (uintptr_t)-EINVAL;

  /* mmap owns its dedicated high anonymous range; ELF and brk remain below
   * the user stack. SPA ownership alone is insufficient: short ELF
   * segments are also copied into SPA pages and must remain loader-owned.
   * Linux permits holes, but a mapped supervisor, loader, swapped, or brk
   * entry must never be freed here. */
  for (size_t i = 0; i < page_count; ++i) {
    pte *entry = pte_of_va((first_vpn + i) << RISCV_PAGE_BITS);
    if (entry && *entry != 0) {
      uintptr_t mapped_page;
      if (((*entry & (PTE_V | PTE_U)) != (PTE_V | PTE_U) ||
           !(*entry & (PTE_R | PTE_W | PTE_X))) &&
          !pte_is_prot_none(*entry))
        return (uintptr_t)-EINVAL;
      mapped_page = __va(pte_ppn(*entry) << RISCV_PAGE_BITS);
      if (!spa_owns(mapped_page))
        return (uintptr_t)-EINVAL;
    }
  }

  free_pages(first_vpn, page_count);
  tlb_flush();
  return 0;
}

uintptr_t syscall_mmap(void *addr, size_t length, int prot, int flags,
                       int fd, uintptr_t offset){
  uintptr_t ret = (uintptr_t)-ENOMEM;

  int pte_flags = PTE_U | PTE_A;
  uintptr_t region_start_vpn = vpn(EYRIE_ANON_REGION_START);
  uintptr_t region_end_vpn = vpn(EYRIE_ANON_REGION_END);
  uintptr_t rounded_length = 0;
  size_t req_pages = 0;

  if (length == 0 ||
      flags != (MAP_ANONYMOUS | MAP_PRIVATE) ||
      (prot & ~(PROT_READ | PROT_WRITE | PROT_EXEC)) != 0 ||
      !IS_ALIGNED(offset, RISCV_PAGE_BITS)) {
    ret = (uintptr_t)-EINVAL;
    goto done;
  }
  if (page_up_safe((uintptr_t)length, &rounded_length) != 0)
    goto done;

  /* Linux ignores fd and the page-aligned offset for anonymous mappings. */
  (void)fd;

  // Set flags
  if(prot & PROT_READ)
    pte_flags |= PTE_R;
  if(prot & PROT_WRITE)
    pte_flags |= PTE_R | PTE_W | PTE_D;
  if(prot & PROT_EXEC)
    pte_flags |= PTE_X;



  // Find a continuous VA space that will fit the req. size
  req_pages = rounded_length >> RISCV_PAGE_BITS;

  // Do we have enough available phys pages?
  if( req_pages > spa_available()){
    goto done;
  }

  // Start looking at EYRIE_ANON_REGION_START for VA space
  uintptr_t starting_vpn = region_start_vpn;
  uintptr_t valid_pages;
  if (req_pages > region_end_vpn - region_start_vpn)
    goto done;
  while (starting_vpn <= region_end_vpn - req_pages) {
    valid_pages = test_va_range(starting_vpn, req_pages);

    if(req_pages == valid_pages){
      if(alloc_pages(starting_vpn, req_pages, pte_flags) == req_pages){
        ret = starting_vpn << RISCV_PAGE_BITS;
      }
      break;
    }
    else
      starting_vpn += valid_pages + 1;
  }

 done:
  if ((intptr_t)ret >= 0)
    tlb_flush();
  print_strace("[runtime] [mmap]: addr: 0x%p, length %lu, prot 0x%x, flags 0x%x, fd %i, offset %lu (%li pages %x) = 0x%p\r\n", addr, length, prot, flags, fd, offset, req_pages, pte_flags, ret);

  // If we get here everything went wrong
  return ret;
}

uintptr_t syscall_mprotect(void *addr, size_t len, int prot) {
  uintptr_t first_vpn;
  size_t pages;

  int pte_flags = PTE_U | PTE_A;
  if ((prot & ~(PROT_READ | PROT_WRITE | PROT_EXEC)) != 0)
    return (uintptr_t)-EINVAL;
  if (!IS_ALIGNED((uintptr_t)addr, RISCV_PAGE_BITS))
    return (uintptr_t)-EINVAL;
  if (len == 0)
    return 0;
  if (page_range((uintptr_t)addr, len, &first_vpn, &pages) != 0)
    return (uintptr_t)-ENOMEM;
  if (user_leaf_range(first_vpn, pages) != 0)
    return (uintptr_t)-ENOMEM;

  if(prot & PROT_READ)
    pte_flags |= PTE_R;
  if(prot & PROT_WRITE)
    pte_flags |= (PTE_R | PTE_W | PTE_D);
  if(prot & PROT_EXEC)
    pte_flags |= PTE_X;

  for (size_t i = 0; i < pages; i++) {
    if (!realloc_page(first_vpn + i, pte_flags))
      return (uintptr_t)-ENOMEM;
  }

  tlb_flush();
  return 0;
}

uintptr_t syscall_brk(void* addr){
  // Two possible valid calls to brk we handle:
  // NULL -> give current break
  // ADDR -> give more pages up to ADDR if possible

  uintptr_t req_break = (uintptr_t)addr;

  uintptr_t current_break = get_program_break();
  uintptr_t minimum_break = get_program_break_minimum();
  uintptr_t current_page_end;
  uintptr_t requested_page_end;
  uintptr_t ret = current_break;
  size_t req_page_count = 0;

  // Return current break if null or current break
  if (req_break == 0) {
    ret = current_break;
    goto done;
  }

  if (req_break < minimum_break || req_break > EYRIE_BRK_REGION_END ||
      page_up_safe(current_break, &current_page_end) != 0 ||
      page_up_safe(req_break, &requested_page_end) != 0)
    goto done;

  if (requested_page_end < current_page_end) {
    size_t release_count =
        (current_page_end - requested_page_end) >> RISCV_PAGE_BITS;
    free_pages(vpn(requested_page_end), release_count);
  } else if (requested_page_end > current_page_end) {
    req_page_count =
        (requested_page_end - current_page_end) >> RISCV_PAGE_BITS;
    if (req_page_count > spa_available() ||
        test_va_range(vpn(current_page_end), req_page_count) !=
            req_page_count ||
        alloc_pages(vpn(current_page_end), req_page_count,
                    PTE_W | PTE_R | PTE_D | PTE_U | PTE_A) !=
            req_page_count)
      goto done;
  }

  set_program_break(req_break);
  ret = req_break;
  if (requested_page_end != current_page_end)
    tlb_flush();

 done:
  print_strace("[runtime] brk (0x%p) (req pages %lu) = 0x%p\r\n",
               req_break, req_page_count, ret);
  return ret;

}
#endif /* USE_LINUX_SYSCALL */
