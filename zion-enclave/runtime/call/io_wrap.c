#ifdef USE_IO_SYSCALL
#define _GNU_SOURCE
#include <stdint.h>
#include "call/io_wrap.h"
#include <alloca.h>
#include "uaccess.h"
#include "call/syscall.h"
#include "util/string.h"
#include "edge_syscall.h"
#include <fcntl.h>
#include <sys/epoll.h>

/* Syscalls iozone uses in -i0 mode
*** Fake these
 *   uname
*** odd
    rt_sigaction
    rt_sigprocmask
*** hard
    brk
    mmap
*/

#define MAX_STRACE_PRINT 20
#define IO_USER_PATH_MAX 4096
#define EYRIE_IOV_MAX 1024

/* Keep the command classes in sync with the host edge dispatcher. Unknown
 * fcntl commands are not scalar by default: several Linux extensions treat
 * the third argument as a pointer in the host process. */
static int fcntl_uses_flock(int cmd) {
  switch (cmd) {
  case F_GETLK:
  case F_SETLK:
  case F_SETLKW:
#ifdef F_OFD_GETLK
  case F_OFD_GETLK:
  case F_OFD_SETLK:
  case F_OFD_SETLKW:
#endif
    return 1;
  default:
    return 0;
  }
}

static int fcntl_uses_scalar(int cmd) {
  switch (cmd) {
  case F_DUPFD:
  case F_GETFD:
  case F_SETFD:
  case F_GETFL:
  case F_SETFL:
  case F_SETOWN:
  case F_GETOWN:
#ifdef F_DUPFD_CLOEXEC
  case F_DUPFD_CLOEXEC:
#endif
#ifdef F_SETPIPE_SZ
  case F_SETPIPE_SZ:
  case F_GETPIPE_SZ:
#endif
#ifdef F_ADD_SEALS
  case F_ADD_SEALS:
  case F_GET_SEALS:
#endif
    return 1;
  default:
    return 0;
  }
}

static int load_user_iovec(const struct iovec *iov, size_t index,
                           struct iovec *result) {
  uintptr_t base = (uintptr_t)iov;
  uintptr_t offset;

  if (result == NULL || index > UINTPTR_MAX / sizeof(*iov))
    return -1;
  offset = index * sizeof(*iov);
  if (base > UINTPTR_MAX - offset ||
      copy_from_user(result, (const void *)(base + offset),
                     sizeof(*result)) != 0)
    return -1;
  return 0;
}

static int validate_user_iovecs(const struct iovec *iov, int iovcnt,
                                size_t *total_length) {
  size_t total = 0;

  if (total_length == NULL || iovcnt < 0 || iovcnt > EYRIE_IOV_MAX ||
      (iovcnt != 0 && iov == NULL))
    return -1;

  for (int i = 0; i < iovcnt; ++i) {
    struct iovec current;
    if (load_user_iovec(iov, (size_t)i, &current) != 0 ||
        current.iov_len > (size_t)INTPTR_MAX - total)
      return -1;
    total += current.iov_len;
  }

  *total_length = total;
  return 0;
}

static int copy_user_string(char *dst, const char *src, size_t capacity,
                            size_t *copied) {
  uintptr_t src_addr = (uintptr_t)src;

  if (dst == NULL || src == NULL || copied == NULL || capacity == 0)
    return -1;

  *copied = 0;
  for (size_t i = 0; i < capacity; ++i) {
    uintptr_t dst_addr = (uintptr_t)dst + i;
    if (dst_addr < (uintptr_t)dst || src_addr > UINTPTR_MAX - i ||
        edge_call_check_ptr_valid(dst_addr, 1) != 0 ||
        copy_from_user((void *)dst_addr, (void *)(src_addr + i), 1) != 0)
      return -1;
    if (*(char *)dst_addr == '\0') {
      *copied = i + 1;
      return 0;
    }
  }
  return -1;
}

static int shared_string_size(const char *src, size_t capacity,
                              size_t *size) {
  if (src == NULL || size == NULL || capacity == 0 ||
      edge_call_check_ptr_valid((uintptr_t)src, capacity) != 0)
    return -1;

  for (size_t i = 0; i < capacity; ++i) {
    if (src[i] == '\0') {
      *size = i + 1;
      return 0;
    }
  }
  return -1;
}

uintptr_t io_syscall_sync(){
  struct edge_syscall* edge_syscall = (struct edge_syscall*)edge_call_data_ptr();

  edge_syscall->syscall_num = SYS_sync;

  size_t totalsize = (sizeof(struct edge_syscall));

  uintptr_t ret = dispatch_edgecall_syscall(edge_syscall, totalsize);
  print_strace("[runtime] proxied sync\r\n");
  return ret;
}

uintptr_t io_syscall_ftruncate(int fd, off_t offset){
  struct edge_syscall* edge_syscall = (struct edge_syscall*)edge_call_data_ptr();
  sargs_SYS_ftruncate* args = (sargs_SYS_ftruncate*)edge_syscall->data;
  edge_syscall->syscall_num = SYS_ftruncate;

  args->fd = fd;
  args->offset = offset;

  size_t totalsize = (sizeof(struct edge_syscall)+
                      sizeof(sargs_SYS_ftruncate));

  uintptr_t ret = dispatch_edgecall_syscall(edge_syscall, totalsize);
  print_strace("[runtime] proxied ftruncate (%i) = %li\r\n", fd, ret);
  return ret;
}
uintptr_t io_syscall_fsync(int fd){
  struct edge_syscall* edge_syscall = (struct edge_syscall*)edge_call_data_ptr();
  sargs_SYS_fsync* args = (sargs_SYS_fsync*)edge_syscall->data;
  edge_syscall->syscall_num = SYS_fsync;

  args->fd = fd;

  size_t totalsize = (sizeof(struct edge_syscall)+
                      sizeof(sargs_SYS_fsync));

  uintptr_t ret = dispatch_edgecall_syscall(edge_syscall, totalsize);
  print_strace("[runtime] proxied fsync (%i) = %li\r\n", fd, ret);
  return ret;
}

uintptr_t io_syscall_lseek(int fd, off_t offset, int whence){
  struct edge_syscall* edge_syscall = (struct edge_syscall*)edge_call_data_ptr();
  sargs_SYS_lseek* args = (sargs_SYS_lseek*)edge_syscall->data;
  edge_syscall->syscall_num = SYS_lseek;

  args->fd = fd;
  args->offset = offset;
  args->whence = whence;

  size_t totalsize = (sizeof(struct edge_syscall)+
                      sizeof(sargs_SYS_lseek));

  uintptr_t ret = dispatch_edgecall_syscall(edge_syscall, totalsize);
  print_strace("[runtime] proxied lseek (on fd:%i to %li from %i) = %li\r\n",
               fd, offset, whence, ret);
  return ret;
}

uintptr_t io_syscall_close(int fd){
  struct edge_syscall* edge_syscall = (struct edge_syscall*)edge_call_data_ptr();
  sargs_SYS_close* args = (sargs_SYS_close*)edge_syscall->data;
  edge_syscall->syscall_num = SYS_close;

  args->fd = fd;

  size_t totalsize = (sizeof(struct edge_syscall)+
                      sizeof(sargs_SYS_close));

  uintptr_t ret = dispatch_edgecall_syscall(edge_syscall, totalsize);
  print_strace("[runtime] proxied close (%i) = %li\r\n", fd, ret);
  return ret;
}

uintptr_t io_syscall_read(int fd, void* buf, size_t len){
  struct edge_syscall* edge_syscall = (struct edge_syscall*)edge_call_data_ptr();
  sargs_SYS_read* args = (sargs_SYS_read*)edge_syscall->data;
  uintptr_t ret = -1;
  edge_syscall->syscall_num = SYS_read;
  args->fd =fd;
  args->len = len;

  // Sanity check that the read buffer will fit in the shared memory
  if(edge_call_check_ptr_valid((uintptr_t)args->buf, len) != 0){
    goto done;
  }

  size_t totalsize = (sizeof(struct edge_syscall) +
                      sizeof(sargs_SYS_read) +
                      len);

  ret = dispatch_edgecall_syscall(edge_syscall, totalsize);

  if ((intptr_t)ret < 0 || ret > len) {
    ret = (uintptr_t)-1;
    goto done;
  }

  // Previously checked that this is staying in untrusted buffer range
  if (copy_to_user(buf, args->buf, ret > len ? len : ret) != 0)
    ret = (uintptr_t)-1;

 done:
  print_strace("[runtime] proxied read from %i (size: %lu) = %li\r\n",fd, len, ret);
  return ret;
}

uintptr_t io_syscall_write(int fd, void* buf, size_t len){
  /* print_strace("[write] len :%lu\r\n", len); */
  /* if(len > 0){ */
  /*   size_t stracelen = len > MAX_STRACE_PRINT? MAX_STRACE_PRINT:len; */
  /*   char* lbuf[MAX_STRACE_PRINT+1]; */
  /*   memset(lbuf, 0, sizeof(lbuf)); */
  /*   copy_from_user(lbuf, (void*)buf, stracelen); */
  /*   print_strace("[write] \"%s\"\r\n", (char*)lbuf); */
  /* } */

  struct edge_syscall* edge_syscall = (struct edge_syscall*)edge_call_data_ptr();
  sargs_SYS_write* args = (sargs_SYS_write*)edge_syscall->data;
  uintptr_t ret = -1;

  edge_syscall->syscall_num = SYS_write;
  args->fd =fd;
  args->len = len;

  // Sanity check that the write buffer will fit in the shared memory
  if(edge_call_check_ptr_valid((uintptr_t)args->buf, len) != 0){
    goto done;
  }

  if (copy_from_user(args->buf, buf, len) != 0)
    goto done;

  size_t totalsize = (sizeof(struct edge_syscall) +
                      sizeof(sargs_SYS_write) +
                      len);

  ret = dispatch_edgecall_syscall(edge_syscall, totalsize);
  if ((intptr_t)ret > (intptr_t)len)
    ret = (uintptr_t)-1;

 done:
  print_strace("[runtime] proxied write to %i (size: %lu) = %li\r\n",fd, len, ret);
  return ret;
}

uintptr_t io_syscall_openat(int dirfd, char* path,
                            int flags, mode_t mode){
  struct edge_syscall* edge_syscall = (struct edge_syscall*)edge_call_data_ptr();
  sargs_SYS_openat* args = (sargs_SYS_openat*)edge_syscall->data;

  edge_syscall->syscall_num = SYS_openat;
  args->dirfd = dirfd;
  args->flags = flags;
  args->mode = mode;
  uintptr_t ret = -1;

  size_t pathlen = 0;
  if (copy_user_string(args->path, path, IO_USER_PATH_MAX, &pathlen) != 0) {
    goto done;
  }

  size_t totalsize = (sizeof(struct edge_syscall) +
                      sizeof(sargs_SYS_openat) +
                      pathlen);

  ret = dispatch_edgecall_syscall(edge_syscall, totalsize);


 done:
  // TODO path print here isn't necessarily correct or even copied!
  print_strace("[runtime] proxied openat(path: %.*s) = %li\r\n",
               pathlen>MAX_STRACE_PRINT?MAX_STRACE_PRINT:pathlen,args->path, ret);

  return ret;
}

uintptr_t io_syscall_unlinkat(int dirfd, char* path,
                              int flags){
  struct edge_syscall* edge_syscall = (struct edge_syscall*)edge_call_data_ptr();
  sargs_SYS_unlinkat* args = (sargs_SYS_unlinkat*)edge_syscall->data;
  uintptr_t ret = -1;

  edge_syscall->syscall_num = SYS_unlinkat;
  args->dirfd = dirfd;
  args->flags = flags;
  size_t pathlen = 0;
  if (copy_user_string(args->path, path, IO_USER_PATH_MAX, &pathlen) != 0) {
    goto done;
  }

  size_t totalsize = (sizeof(struct edge_syscall) +
                      sizeof(sargs_SYS_unlinkat) +
                      pathlen);

  ret = dispatch_edgecall_syscall(edge_syscall, totalsize);


 done:
  // TODO path print here isn't necessarily correct or even copied!
  print_strace("[runtime] proxied unlinkat(path: %.*s) = %li\r\n",
               pathlen>MAX_STRACE_PRINT?MAX_STRACE_PRINT:pathlen,args->path, ret);
  return ret;
}

uintptr_t io_syscall_writev(int fd, const struct iovec *iov, int iovcnt){
  int i=0;
  intptr_t ret = 0;
  size_t total = 0;
  print_strace("[runtime] Simulating writev (cnt %i) with write calls\r\n",iovcnt);
  if (validate_user_iovecs(iov, iovcnt, &total) != 0)
    return (uintptr_t)-1;
  total = 0;
  for(i=0; i<iovcnt && ret >= 0;i++){
    struct iovec iov_local;
    if (load_user_iovec(iov, (size_t)i, &iov_local) != 0)
      return total == 0 ? (uintptr_t)-1 : total;
    ret = (intptr_t)io_syscall_write(fd, iov_local.iov_base,
                                    iov_local.iov_len);
    if (ret > 0)
      total += (size_t)ret;
  }
  if (ret < 0 && total == 0)
    return (uintptr_t)-1;
  print_strace("[runtime] Simulated writev = %li\r\n", total);
  return total;
}

uintptr_t io_syscall_readv(int fd, const struct iovec *iov, int iovcnt){
  int i=0;
  intptr_t ret = 0;
  size_t total = 0;
  print_strace("[runtime] Simulating readv (cnt %i) with read calls\r\n",iovcnt);
  if (validate_user_iovecs(iov, iovcnt, &total) != 0)
    return (uintptr_t)-1;
  total = 0;
  for(i=0; i<iovcnt && ret >= 0;i++){
    struct iovec iov_local;
    if (load_user_iovec(iov, (size_t)i, &iov_local) != 0)
      return total == 0 ? (uintptr_t)-1 : total;
    ret = (intptr_t)io_syscall_read(fd, iov_local.iov_base,
                                   iov_local.iov_len);
    if (ret > 0)
      total += (size_t)ret;
  }

  if (ret < 0 && total == 0)
    return (uintptr_t)-1;
  print_strace("[runtime] Simulated readv = %li\r\n", total);
  return total;
}

uintptr_t io_syscall_fstatat(int dirfd, char *pathname, struct stat *statbuf,
                                int flags){
  struct edge_syscall* edge_syscall = (struct edge_syscall*)edge_call_data_ptr();
  sargs_SYS_fstatat* args = (sargs_SYS_fstatat*)edge_syscall->data;
  uintptr_t ret = -1;

  edge_syscall->syscall_num = SYS_fstatat;
  args->dirfd = dirfd;
  args->flags = flags;

  size_t pathlen = 0;
  if (copy_user_string(
          args->pathname, pathname, IO_USER_PATH_MAX, &pathlen) != 0) {
    goto done;
  }

  size_t totalsize = (sizeof(struct edge_syscall) +
                      sizeof(sargs_SYS_fstatat) +
                      pathlen);

  ret = dispatch_edgecall_syscall(edge_syscall, totalsize);

  if(ret == 0 && copy_to_user(statbuf, &args->stats, sizeof(struct stat)) != 0)
    ret = (uintptr_t)-1;

 done:
  print_strace("[runtime] proxied fstatat (path %.*s) = %li\r\n",
               pathlen>MAX_STRACE_PRINT?MAX_STRACE_PRINT:pathlen,args->pathname, ret);
  return ret;

}

uintptr_t io_syscall_pipe2(int *fds, int flags){

  uintptr_t ret = -1;
  int original_fds[2];
  struct edge_syscall* edge_syscall = (struct edge_syscall*)edge_call_data_ptr();
  edge_syscall->syscall_num = SYS_pipe2;

  sargs_SYS_pipe2 *args = (sargs_SYS_pipe2 *)edge_syscall->data;
  args->flags = flags;

  /* pipe2 allocates two host descriptors. Prove the enclave destination is
   * readable and writable before allocating them so a faulting copyout cannot
   * turn into a host descriptor leak in the single-threaded runtime. */
  if (copy_from_user(original_fds, fds, sizeof(original_fds)) != 0 ||
      copy_to_user(fds, original_fds, sizeof(original_fds)) != 0)
    return ret;

  size_t totalsize = sizeof(struct edge_syscall) + sizeof(*args);

  ret = dispatch_edgecall_syscall(edge_syscall, totalsize);

  if (ret == 0 && copy_to_user(fds, args->fds, sizeof(args->fds)) != 0)
    ret = (uintptr_t)-1;

  print_strace("[runtime] proxied pipe2 \r\n");
  return ret;
}

uintptr_t io_syscall_epoll_create1(int flags){
  uintptr_t ret = -1;
  struct edge_syscall* edge_syscall = (struct edge_syscall*)edge_call_data_ptr();
  edge_syscall->syscall_num = SYS_epoll_create1;

  sargs_SYS_epoll_create1 *args = (sargs_SYS_epoll_create1 *) edge_syscall->data;

  args->flags = flags;

  size_t totalsize = sizeof(struct edge_syscall) + sizeof(sargs_SYS_epoll_create1);
  ret = dispatch_edgecall_syscall(edge_syscall, totalsize);

  print_strace("[runtime] proxied epoll_create1: %d \r\n", ret);
  return ret; 
}

uintptr_t io_syscall_epoll_ctl(int epfd, int op, int fd, uintptr_t event){
  uintptr_t ret = -1;
  struct edge_syscall* edge_syscall = (struct edge_syscall*)edge_call_data_ptr();
  edge_syscall->syscall_num = SYS_epoll_ctl;

  sargs_SYS_epoll_ctl *args = (sargs_SYS_epoll_ctl *) edge_syscall->data;

  args->epfd = epfd; 
  args->op = op;
  args->fd = fd; 

  switch (op) {
  case EPOLL_CTL_ADD:
  case EPOLL_CTL_MOD:
    if (event == 0 ||
        copy_from_user(&args->event, (void *)event,
                       sizeof(struct epoll_event)) != 0)
      return ret;
    args->event_is_null = 0;
    break;
  case EPOLL_CTL_DEL:
    args->event_is_null = 1;
    memset(&args->event, 0, sizeof(args->event));
    break;
  default:
    return ret;
  }

  size_t totalsize = sizeof(struct edge_syscall) + sizeof(sargs_SYS_epoll_ctl);
  ret = dispatch_edgecall_syscall(edge_syscall, totalsize);

  print_strace("[runtime] proxied epoll_ctl: %d \r\n", ret);
  return ret; 
}

uintptr_t io_syscall_fcntl(int fd, int cmd, uintptr_t arg){ 
  struct edge_syscall* edge_syscall = (struct edge_syscall*)edge_call_data_ptr();
  sargs_SYS_fcntl* args = (sargs_SYS_fcntl*)edge_syscall->data;
  uintptr_t ret = -1;

  edge_syscall->syscall_num = SYS_fcntl;
  args->fd = fd;
  args->cmd = cmd;

  size_t totalsize;
  if (fcntl_uses_flock(cmd)) {
    print_strace("fcntl flock command");
    if(edge_call_check_ptr_valid((uintptr_t)args->arg, sizeof(struct flock)) != 0){
      print_strace("Ptr not valid");
      goto done;
    }
    if (copy_from_user((struct flock *)args->arg, (struct flock *)arg,
                       sizeof(struct flock)) != 0)
      goto done;
    args->has_struct = 1;

    totalsize = (sizeof(struct edge_syscall) +
                      sizeof(sargs_SYS_fcntl) + 
                      sizeof(struct flock));
  } else if (fcntl_uses_scalar(cmd)) {
    args->arg[0] = arg;
    args->has_struct = 0;
    totalsize = (sizeof(struct edge_syscall) +
                      sizeof(sargs_SYS_fcntl) + sizeof(unsigned long));
  } else {
    goto done;
  }



  ret = dispatch_edgecall_syscall(edge_syscall, totalsize);

  if (ret == 0 && (cmd == F_GETLK
#ifdef F_OFD_GETLK
                   || cmd == F_OFD_GETLK
#endif
                   ) &&
      copy_to_user((void *)arg, args->arg, sizeof(struct flock)) != 0)
    ret = (uintptr_t)-1;

 done: 
  print_strace("[runtime] proxied fcntl = %li\r\n", ret);
  return ret;
}

uintptr_t io_syscall_getcwd(char* buf, size_t size){ 
  struct edge_syscall* edge_syscall = (struct edge_syscall*)edge_call_data_ptr();
  sargs_SYS_getcwd* args = (sargs_SYS_getcwd*)edge_syscall->data;

  edge_syscall->syscall_num = SYS_getcwd;

  args->size = size;

  if (edge_call_check_ptr_valid((uintptr_t)args->buf, size) != 0) {
    return (uintptr_t)-1;
  }

  size_t totalsize = (sizeof(struct edge_syscall) +
                      sizeof(sargs_SYS_getcwd) + size);

  uintptr_t ret = dispatch_edgecall_syscall(edge_syscall, totalsize);
  if ((intptr_t)ret < 0) {
    return ret;
  }

  size_t result_size;
  if (ret == 0 || ret > size ||
      shared_string_size(args->buf, ret, &result_size) != 0 ||
      result_size != ret || copy_to_user(buf, args->buf, ret) != 0)
    return (uintptr_t)-1;
  print_strace("[runtime] proxied getcwd\r\n");
  return ret;
}

uintptr_t io_syscall_chdir(char* path) { 

  uintptr_t ret = -1;
  struct edge_syscall* edge_syscall = (struct edge_syscall*)edge_call_data_ptr();
  sargs_SYS_chdir* args = (sargs_SYS_chdir*)edge_syscall->data;
  // char* syscall_ret = NULL;

  edge_syscall->syscall_num = SYS_chdir;

  size_t pathlen = 0;
  if (copy_user_string(args->path, path, IO_USER_PATH_MAX, &pathlen) != 0)
    return ret;

  size_t totalsize = sizeof(struct edge_syscall) +
                     sizeof(sargs_SYS_chdir) + pathlen;
  ret = dispatch_edgecall_syscall(edge_syscall, totalsize);

  print_strace("[runtime] proxied chdir: %s\r\n", args->path);
  return ret;
}


uintptr_t io_syscall_epoll_pwait(int epfd, uintptr_t events, int maxevents,
                                 int timeout, uintptr_t sigmask,
                                 size_t sigsetsize) {
  uintptr_t ret = -1;
  struct edge_syscall* edge_syscall = (struct edge_syscall*)edge_call_data_ptr();
  sargs_SYS_epoll_pwait* args = (sargs_SYS_epoll_pwait*) edge_syscall->data;

  edge_syscall->syscall_num = SYS_epoll_pwait;

  args->epfd = epfd;
  args->maxevents = maxevents;
  args->timeout = timeout;
  args->sigmask_is_null = sigmask == 0;
  args->sigsetsize = sigsetsize;

  /* Signal-mask replacement is not implemented by this proxy. Reject it
   * explicitly instead of silently executing epoll_wait semantics. */
  if (maxevents != 1 || sigmask != 0 || sigsetsize != 0) {
    return (uintptr_t)-1;
  }

  size_t totalsize = (sizeof(struct edge_syscall)) + sizeof(sargs_SYS_epoll_pwait);
  ret = dispatch_edgecall_syscall(edge_syscall, totalsize);

  if ((intptr_t)ret > maxevents) {
    ret = (uintptr_t)-1;
  } else if ((intptr_t)ret > 0 &&
      copy_to_user((void *)events, &args->events,
                   sizeof(struct epoll_event)) != 0)
    ret = (uintptr_t)-1;
  print_strace("[runtime] proxied epoll_pwait: epfd: %d, ret: %d\r\n", args->epfd, ret);
  return ret;
}

uintptr_t io_syscall_renameat2(int olddirfd,  uintptr_t oldpath, int newdirfd, uintptr_t newpath, unsigned int flags){
  uintptr_t ret = -1;
  struct edge_syscall* edge_syscall = (struct edge_syscall*)edge_call_data_ptr();
  sargs_SYS_renameat2* args = (sargs_SYS_renameat2*) edge_syscall->data;

  edge_syscall->syscall_num = SYS_renameat2;

  args->olddirfd = olddirfd;
  args->newdirfd = newdirfd;
  args->flags = flags;

  size_t pathlen = 0;
  if (copy_user_string(args->oldpath, (void *)oldpath,
                       sizeof(args->oldpath), &pathlen) != 0 ||
      copy_user_string(args->newpath, (void *)newpath,
                       sizeof(args->newpath), &pathlen) != 0)
    return ret;

  size_t totalsize = (sizeof(struct edge_syscall)) + sizeof(sargs_SYS_renameat2);
  ret = dispatch_edgecall_syscall(edge_syscall, totalsize);

  print_strace("[runtime] proxied renameat2: oldpath: %s, newpath: %s, ret: %d\r\n", args->oldpath, args->newpath, ret);
  return ret;
}

uintptr_t io_syscall_umask(int mask){
  uintptr_t ret = -1;
  struct edge_syscall* edge_syscall = (struct edge_syscall*)edge_call_data_ptr();
  sargs_SYS_umask* args = (sargs_SYS_umask*) edge_syscall->data;

  edge_syscall->syscall_num = SYS_umask;
  args->mask = mask;

  size_t totalsize = (sizeof(struct edge_syscall)) + sizeof(sargs_SYS_umask);
  ret = dispatch_edgecall_syscall(edge_syscall, totalsize);

  print_strace("[runtime] proxied umask: mask: %d, ret: %d\r\n", args->mask, ret);
  return ret;
}
  
uintptr_t io_syscall_fstat(int fd, struct stat *statbuf){
  struct edge_syscall* edge_syscall = (struct edge_syscall*)edge_call_data_ptr();
  sargs_SYS_fstat* args = (sargs_SYS_fstat*)edge_syscall->data;
  uintptr_t ret = -1;

  edge_syscall->syscall_num = SYS_fstat;
  args->fd = fd;

  size_t totalsize = (sizeof(struct edge_syscall) +
                      sizeof(sargs_SYS_fstat));

  ret = dispatch_edgecall_syscall(edge_syscall, totalsize);

  if(ret == 0 && copy_to_user(statbuf, &args->stats, sizeof(struct stat)) != 0)
    ret = (uintptr_t)-1;

  print_strace("[runtime] proxied fstat = %li\r\n", ret);
  return ret;

}

#endif /* USE_IO_SYSCALL */
