#include <stddef.h>
#include <stdint.h>
#include <sys/stat.h>

#include "app/eapp_utils.h"

#define SYS_GETCWD 17
#define SYS_UNLINKAT 35
#define SYS_FTRUNCATE 46
#define SYS_CHDIR 49
#define SYS_OPENAT 56
#define SYS_CLOSE 57
#define SYS_LSEEK 62
#define SYS_READ 63
#define SYS_WRITE 64
#define SYS_FSTATAT 79
#define SYS_FSTAT 80
#define SYS_FSYNC 82
#define SYS_RENAMEAT2 276

#define AT_FDCWD (-100)
#define O_RDONLY 0
#define O_RDWR 2
#define O_CREAT 0100
#define O_TRUNC 01000
#define SEEK_SET 0
#define ENOENT 2
#define TEST_SUCCESS 31415UL

static const char old_path[] = "/tmp/zion-io-regression.old";
static const char new_path[] = "/tmp/zion-io-regression.new";

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

static void cleanup(int fd)
{
  if (fd >= 0)
    syscall6(SYS_CLOSE, (uintptr_t)fd, 0, 0, 0, 0, 0);
  syscall6(SYS_UNLINKAT, (uintptr_t)AT_FDCWD, (uintptr_t)old_path, 0,
           0, 0, 0);
  syscall6(SYS_UNLINKAT, (uintptr_t)AT_FDCWD, (uintptr_t)new_path, 0,
           0, 0, 0);
  syscall6(SYS_CHDIR, (uintptr_t)"/", 0, 0, 0, 0, 0);
}

void EAPP_ENTRY eapp_entry(void)
{
  static const unsigned char payload[] = "zion-file";
  char cwd[64] = {0};
  unsigned char data[16] = {0};
  struct stat stats;
  uintptr_t result;
  int fd = -1;

  result = syscall6(SYS_GETCWD, (uintptr_t)cwd, sizeof(cwd), 0, 0, 0, 0);
  if (result != 2 || cwd[0] != '/' || cwd[1] != '\0' ||
      syscall6(SYS_GETCWD, UINTPTR_MAX - 3, 8, 0, 0, 0, 0) !=
          (uintptr_t)-1) {
    cleanup(fd);
    EAPP_RETURN(1);
  }

  if ((intptr_t)syscall6(SYS_CHDIR, (uintptr_t)"/tmp", 0, 0, 0, 0, 0) < 0) {
    cleanup(fd);
    EAPP_RETURN(2);
  }
  result = syscall6(SYS_GETCWD, (uintptr_t)cwd, sizeof(cwd), 0, 0, 0, 0);
  if (result != 5 || cwd[0] != '/' || cwd[1] != 't' || cwd[2] != 'm' ||
      cwd[3] != 'p' || cwd[4] != '\0' ||
      (intptr_t)syscall6(SYS_CHDIR, (uintptr_t)"/", 0, 0, 0, 0, 0) < 0) {
    cleanup(fd);
    EAPP_RETURN(3);
  }

  cleanup(fd);
  fd = (int)syscall6(SYS_OPENAT, (uintptr_t)AT_FDCWD,
                     (uintptr_t)old_path, O_CREAT | O_TRUNC | O_RDWR, 0600,
                     0, 0);
  if (fd < 0) {
    cleanup(fd);
    EAPP_RETURN(4);
  }

  if (syscall6(SYS_WRITE, (uintptr_t)fd, UINTPTR_MAX - 3, 8, 0, 0, 0) !=
          (uintptr_t)-1 ||
      syscall6(SYS_WRITE, (uintptr_t)fd, (uintptr_t)payload,
               sizeof(payload) - 1, 0, 0, 0) != sizeof(payload) - 1 ||
      (intptr_t)syscall6(SYS_FTRUNCATE, (uintptr_t)fd, 4, 0, 0, 0, 0) < 0 ||
      (intptr_t)syscall6(SYS_FSYNC, (uintptr_t)fd, 0, 0, 0, 0, 0) < 0) {
    cleanup(fd);
    EAPP_RETURN(5);
  }

  if ((intptr_t)syscall6(SYS_FSTAT, (uintptr_t)fd, (uintptr_t)&stats,
                         0, 0, 0, 0) < 0 ||
      stats.st_size != 4) {
    cleanup(fd);
    EAPP_RETURN(6);
  }

  if (syscall6(SYS_LSEEK, (uintptr_t)fd, 0, SEEK_SET, 0, 0, 0) != 0 ||
      syscall6(SYS_READ, (uintptr_t)fd, UINTPTR_MAX - 3, 8, 0, 0, 0) !=
          (uintptr_t)-1 ||
      syscall6(SYS_LSEEK, (uintptr_t)fd, 0, SEEK_SET, 0, 0, 0) != 0 ||
      syscall6(SYS_READ, (uintptr_t)fd, (uintptr_t)data, sizeof(data),
               0, 0, 0) != 4 ||
      syscall6(SYS_READ, (uintptr_t)fd, (uintptr_t)data, sizeof(data),
               0, 0, 0) != 0 ||
      data[0] != 'z' || data[1] != 'i' || data[2] != 'o' ||
      data[3] != 'n') {
    cleanup(fd);
    EAPP_RETURN(7);
  }

  if ((intptr_t)syscall6(SYS_CLOSE, (uintptr_t)fd, 0, 0, 0, 0, 0) < 0) {
    cleanup(-1);
    EAPP_RETURN(8);
  }
  fd = -1;

  if ((intptr_t)syscall6(SYS_RENAMEAT2, (uintptr_t)AT_FDCWD,
                         (uintptr_t)old_path, (uintptr_t)AT_FDCWD,
                         (uintptr_t)new_path, 0, 0) < 0 ||
      (intptr_t)syscall6(SYS_FSTATAT, (uintptr_t)AT_FDCWD,
                         (uintptr_t)new_path, (uintptr_t)&stats, 0, 0, 0) < 0 ||
      stats.st_size != 4 ||
      syscall6(SYS_OPENAT, (uintptr_t)AT_FDCWD, (uintptr_t)old_path,
               O_RDONLY, 0, 0, 0) != (uintptr_t)-ENOENT ||
      (intptr_t)syscall6(SYS_UNLINKAT, (uintptr_t)AT_FDCWD,
                         (uintptr_t)new_path, 0, 0, 0, 0) < 0 ||
      syscall6(SYS_FSTATAT, (uintptr_t)AT_FDCWD, (uintptr_t)new_path,
               (uintptr_t)&stats, 0, 0, 0) != (uintptr_t)-ENOENT) {
    cleanup(fd);
    EAPP_RETURN(9);
  }

  cleanup(fd);
  EAPP_RETURN(TEST_SUCCESS);
}
