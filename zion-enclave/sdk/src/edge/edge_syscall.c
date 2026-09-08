#define _GNU_SOURCE
#include "edge_syscall.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/select.h>
#include <sys/sendfile.h>
#include <sys/socket.h>
#include <unistd.h>

static int
edge_syscall_args_available(size_t args_size, size_t fixed, size_t flexible) {
  size_t payload_size;

  if (args_size < sizeof(struct edge_syscall)) return 0;
  payload_size = args_size - sizeof(struct edge_syscall);
  return fixed <= payload_size && flexible <= payload_size - fixed;
}

static int
edge_syscall_string_terminated(const char* string, size_t available) {
  return available != 0 && memchr(string, '\0', available) != NULL;
}

/* Never infer that an unknown fcntl command has a scalar third argument.
 * Linux has pointer-valued extensions, and forwarding an enclave-controlled
 * integer for one of those would make libc dereference a host address. Keep
 * these command classes in sync with runtime/call/io_wrap.c. */
static int
edge_fcntl_uses_flock(int cmd) {
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

static int
edge_fcntl_uses_scalar(int cmd) {
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

#define REQUIRE_FIXED(type)                                                   \
  do {                                                                        \
    if (!edge_syscall_args_available(args_size, sizeof(type), 0))             \
      goto syscall_error;                                                     \
  } while (0)

#define REQUIRE_FLEX(type, length)                                            \
  do {                                                                        \
    if (!edge_syscall_args_available(args_size, sizeof(type), (length)))      \
      goto syscall_error;                                                     \
  } while (0)

/* Special edge-call handler for syscall proxying. Every request field is in
 * enclave-controlled shared memory, so validate the complete fixed/flexible
 * payload before passing any pointer or length to the host kernel. */
void
incoming_syscall(struct edge_call* edge_call) {
  struct edge_syscall* syscall_info;
  size_t args_size;
  int64_t ret;

  if (edge_call_args_ptr(
          edge_call, (uintptr_t*)&syscall_info, &args_size) != 0 ||
      args_size < sizeof(struct edge_syscall))
    goto syscall_error;

  edge_call->return_data.call_status = CALL_STATUS_SYSCALL_FAILED;
  edge_call->return_data.call_ret_offset = 0;
  edge_call->return_data.call_ret_size = 0;

  switch (syscall_info->syscall_num) {
    case SYS_openat: {
      sargs_SYS_openat* args;
      size_t path_size;

      REQUIRE_FLEX(sargs_SYS_openat, 1);
      args = (sargs_SYS_openat*)syscall_info->data;
      path_size = args_size - sizeof(struct edge_syscall) -
                  sizeof(sargs_SYS_openat);
      if (!edge_syscall_string_terminated(args->path, path_size))
        goto syscall_error;
      ret = openat(args->dirfd, args->path, args->flags, args->mode);
      break;
    }
    case SYS_unlinkat: {
      sargs_SYS_unlinkat* args;
      size_t path_size;

      REQUIRE_FLEX(sargs_SYS_unlinkat, 1);
      args = (sargs_SYS_unlinkat*)syscall_info->data;
      path_size = args_size - sizeof(struct edge_syscall) -
                  sizeof(sargs_SYS_unlinkat);
      if (!edge_syscall_string_terminated(args->path, path_size))
        goto syscall_error;
      ret = unlinkat(args->dirfd, args->path, args->flags);
      break;
    }
    case SYS_ftruncate: {
      REQUIRE_FIXED(sargs_SYS_ftruncate);
      sargs_SYS_ftruncate* args =
          (sargs_SYS_ftruncate*)syscall_info->data;
      ret = ftruncate(args->fd, args->offset);
      break;
    }
    case SYS_fstatat: {
      sargs_SYS_fstatat* args;
      size_t path_size;

      REQUIRE_FLEX(sargs_SYS_fstatat, 1);
      args = (sargs_SYS_fstatat*)syscall_info->data;
      path_size = args_size - sizeof(struct edge_syscall) -
                  sizeof(sargs_SYS_fstatat);
      if (!edge_syscall_string_terminated(args->pathname, path_size))
        goto syscall_error;
      ret = fstatat(
          args->dirfd, args->pathname, &args->stats, args->flags);
      break;
    }
    case SYS_fstat: {
      REQUIRE_FIXED(sargs_SYS_fstat);
      sargs_SYS_fstat* args = (sargs_SYS_fstat*)syscall_info->data;
      ret = fstat(args->fd, &args->stats);
      break;
    }
    case SYS_getcwd: {
      char* result;
      REQUIRE_FIXED(sargs_SYS_getcwd);
      sargs_SYS_getcwd* args = (sargs_SYS_getcwd*)syscall_info->data;
      REQUIRE_FLEX(sargs_SYS_getcwd, args->size);
      result = getcwd(args->buf, args->size);
      ret = result == NULL ? -1 : (int64_t)(strlen(result) + 1);
      break;
    }
    case SYS_write: {
      REQUIRE_FIXED(sargs_SYS_write);
      sargs_SYS_write* args = (sargs_SYS_write*)syscall_info->data;
      REQUIRE_FLEX(sargs_SYS_write, args->len);
      ret = write(args->fd, args->buf, args->len);
      break;
    }
    case SYS_read: {
      REQUIRE_FIXED(sargs_SYS_read);
      sargs_SYS_read* args = (sargs_SYS_read*)syscall_info->data;
      REQUIRE_FLEX(sargs_SYS_read, args->len);
      ret = read(args->fd, args->buf, args->len);
      break;
    }
    case SYS_sync:
      sync();
      ret = 0;
      break;
    case SYS_fsync: {
      REQUIRE_FIXED(sargs_SYS_fsync);
      sargs_SYS_fsync* args = (sargs_SYS_fsync*)syscall_info->data;
      ret = fsync(args->fd);
      break;
    }
    case SYS_close: {
      REQUIRE_FIXED(sargs_SYS_close);
      sargs_SYS_close* args = (sargs_SYS_close*)syscall_info->data;
      ret = close(args->fd);
      break;
    }
    case SYS_lseek: {
      REQUIRE_FIXED(sargs_SYS_lseek);
      sargs_SYS_lseek* args = (sargs_SYS_lseek*)syscall_info->data;
      ret = lseek(args->fd, args->offset, args->whence);
      break;
    }
    case SYS_pipe2: {
      REQUIRE_FIXED(sargs_SYS_pipe2);
      sargs_SYS_pipe2* args = (sargs_SYS_pipe2*)syscall_info->data;
      ret = pipe2(args->fds, args->flags);
      break;
    }
    case SYS_epoll_create1: {
      REQUIRE_FIXED(sargs_SYS_epoll_create1);
      sargs_SYS_epoll_create1* args =
          (sargs_SYS_epoll_create1*)syscall_info->data;
      ret = epoll_create1(args->flags);
      break;
    }
    case SYS_chdir: {
      sargs_SYS_chdir* args;
      size_t path_size;

      REQUIRE_FLEX(sargs_SYS_chdir, 1);
      args = (sargs_SYS_chdir*)syscall_info->data;
      path_size = args_size - sizeof(struct edge_syscall) -
                  sizeof(sargs_SYS_chdir);
      if (!edge_syscall_string_terminated(args->path, path_size))
        goto syscall_error;
      ret = chdir(args->path);
      break;
    }
    case SYS_epoll_ctl: {
      struct epoll_event* event;

      REQUIRE_FIXED(sargs_SYS_epoll_ctl);
      sargs_SYS_epoll_ctl* args =
          (sargs_SYS_epoll_ctl*)syscall_info->data;
      if ((args->op == EPOLL_CTL_ADD || args->op == EPOLL_CTL_MOD) &&
          args->event_is_null)
        goto syscall_error;
      if (args->op == EPOLL_CTL_DEL) {
        if (!args->event_is_null) goto syscall_error;
      } else if (args->op != EPOLL_CTL_ADD && args->op != EPOLL_CTL_MOD) {
        goto syscall_error;
      }
      event = args->event_is_null ? NULL : &args->event;
      ret = epoll_ctl(args->epfd, args->op, args->fd, event);
      break;
    }
    case SYS_epoll_pwait: {
      REQUIRE_FIXED(sargs_SYS_epoll_pwait);
      sargs_SYS_epoll_pwait* args =
          (sargs_SYS_epoll_pwait*)syscall_info->data;
      if (args->maxevents != 1 || !args->sigmask_is_null ||
          args->sigsetsize != 0)
        goto syscall_error;
      ret = epoll_wait(
          args->epfd, &args->events, args->maxevents, args->timeout);
      break;
    }
    case SYS_getpeername: {
      REQUIRE_FIXED(sargs_SYS_getpeername);
      sargs_SYS_getpeername* args =
          (sargs_SYS_getpeername*)syscall_info->data;
      if (args->addrlen > sizeof(args->addr)) goto syscall_error;
      ret = getpeername(
          args->sockfd, (struct sockaddr*)&args->addr, &args->addrlen);
      break;
    }
    case SYS_getsockname: {
      REQUIRE_FIXED(sargs_SYS_getsockname);
      sargs_SYS_getsockname* args =
          (sargs_SYS_getsockname*)syscall_info->data;
      if (args->addrlen > sizeof(args->addr)) goto syscall_error;
      ret = getsockname(
          args->sockfd, (struct sockaddr*)&args->addr, &args->addrlen);
      break;
    }
    case SYS_renameat2: {
      REQUIRE_FIXED(sargs_SYS_renameat2);
      sargs_SYS_renameat2* args =
          (sargs_SYS_renameat2*)syscall_info->data;
      if (args->flags != 0 ||
          memchr(args->oldpath, '\0', sizeof(args->oldpath)) == NULL ||
          memchr(args->newpath, '\0', sizeof(args->newpath)) == NULL)
        goto syscall_error;
      ret = renameat(
          args->olddirfd, args->oldpath, args->newdirfd, args->newpath);
      break;
    }
    case SYS_umask: {
      REQUIRE_FIXED(sargs_SYS_umask);
      sargs_SYS_umask* args = (sargs_SYS_umask*)syscall_info->data;
      ret = umask(args->mask);
      break;
    }
    case SYS_socket: {
      REQUIRE_FIXED(sargs_SYS_socket);
      sargs_SYS_socket* args = (sargs_SYS_socket*)syscall_info->data;
      ret = socket(args->domain, args->type, args->protocol);
      break;
    }
    case SYS_setsockopt: {
      REQUIRE_FIXED(sargs_SYS_setsockopt);
      sargs_SYS_setsockopt* args =
          (sargs_SYS_setsockopt*)syscall_info->data;
      REQUIRE_FLEX(sargs_SYS_setsockopt, args->option_len);
      ret = setsockopt(
          args->socket, args->level, args->option_name, args->option_value,
          args->option_len);
      break;
    }
    case SYS_connect: {
      REQUIRE_FIXED(sargs_SYS_connect);
      sargs_SYS_connect* args = (sargs_SYS_connect*)syscall_info->data;
      if (args->addrlen > sizeof(args->addr)) goto syscall_error;
      ret = connect(
          args->sockfd, (struct sockaddr*)&args->addr, args->addrlen);
      break;
    }
    case SYS_bind: {
      REQUIRE_FIXED(sargs_SYS_bind);
      sargs_SYS_bind* args = (sargs_SYS_bind*)syscall_info->data;
      if (args->addrlen > sizeof(args->addr)) goto syscall_error;
      ret = bind(args->sockfd, (struct sockaddr*)&args->addr, args->addrlen);
      break;
    }
    case SYS_listen: {
      REQUIRE_FIXED(sargs_SYS_listen);
      sargs_SYS_listen* args = (sargs_SYS_listen*)syscall_info->data;
      ret = listen(args->sockfd, args->backlog);
      break;
    }
    case SYS_accept: {
      REQUIRE_FIXED(sargs_SYS_accept);
      sargs_SYS_accept* args = (sargs_SYS_accept*)syscall_info->data;
      if (args->addrlen > sizeof(args->addr)) goto syscall_error;
      ret = accept(
          args->sockfd, (struct sockaddr*)&args->addr, &args->addrlen);
      break;
    }
    case SYS_recvfrom: {
      struct sockaddr* source;
      socklen_t* addrlen;

      REQUIRE_FIXED(sargs_SYS_recvfrom);
      sargs_SYS_recvfrom* args =
          (sargs_SYS_recvfrom*)syscall_info->data;
      REQUIRE_FLEX(sargs_SYS_recvfrom, args->len);
      if (!args->src_addr_is_null &&
          args->addrlen > sizeof(args->src_addr))
        goto syscall_error;
      source = args->src_addr_is_null
                   ? NULL
                   : (struct sockaddr*)&args->src_addr;
      addrlen = args->src_addr_is_null ? NULL : &args->addrlen;
      ret = recvfrom(
          args->sockfd, args->buf, args->len, args->flags, source, addrlen);
      break;
    }
    case SYS_sendto: {
      struct sockaddr* destination;
      socklen_t addrlen;

      REQUIRE_FIXED(sargs_SYS_sendto);
      sargs_SYS_sendto* args = (sargs_SYS_sendto*)syscall_info->data;
      REQUIRE_FLEX(sargs_SYS_sendto, args->len);
      if (!args->dest_addr_is_null &&
          args->addrlen > sizeof(args->dest_addr))
        goto syscall_error;
      destination = args->dest_addr_is_null
                        ? NULL
                        : (struct sockaddr*)&args->dest_addr;
      addrlen = args->dest_addr_is_null ? 0 : args->addrlen;
      ret = sendto(
          args->sockfd, args->buf, args->len, args->flags, destination,
          addrlen);
      break;
    }
    case SYS_sendfile: {
      off_t* offset;

      REQUIRE_FIXED(sargs_SYS_sendfile);
      sargs_SYS_sendfile* args =
          (sargs_SYS_sendfile*)syscall_info->data;
      offset = args->offset_is_null ? NULL : &args->offset;
      ret = sendfile(args->out_fd, args->in_fd, offset, args->count);
      break;
    }
    case SYS_fcntl: {
      REQUIRE_FIXED(sargs_SYS_fcntl);
      sargs_SYS_fcntl* args = (sargs_SYS_fcntl*)syscall_info->data;
      if (!args->has_struct && edge_fcntl_uses_scalar(args->cmd)) {
        REQUIRE_FLEX(sargs_SYS_fcntl, sizeof(unsigned long));
        ret = fcntl(args->fd, args->cmd, args->arg[0]);
      } else if (args->has_struct == 1 &&
                 edge_fcntl_uses_flock(args->cmd)) {
        REQUIRE_FLEX(sargs_SYS_fcntl, sizeof(struct flock));
        ret = fcntl(args->fd, args->cmd, args->arg);
      } else
        goto syscall_error;
      break;
    }
    case SYS_getuid:
      ret = getuid();
      break;
    case SYS_pselect6: {
      fd_set* readfds;
      fd_set* writefds;
      fd_set* exceptfds;
      struct timespec* timeout;
      sigset_t* sigmask;

      REQUIRE_FIXED(sargs_SYS_pselect);
      sargs_SYS_pselect* args = (sargs_SYS_pselect*)syscall_info->data;
      if (args->nfds < 0 || args->nfds > FD_SETSIZE) goto syscall_error;
      if ((args->sigmask_is_null && args->sigsetsize != 0) ||
          (!args->sigmask_is_null &&
           args->sigsetsize != sizeof(uint64_t)))
        goto syscall_error;
      readfds = args->readfds_is_null ? NULL : &args->readfds;
      writefds = args->writefds_is_null ? NULL : &args->writefds;
      exceptfds = args->exceptfds_is_null ? NULL : &args->exceptfds;
      timeout = args->timeout_is_null ? NULL : &args->timeout;
      sigmask = args->sigmask_is_null ? NULL : &args->sigmask;
      ret = pselect(
          args->nfds, readfds, writefds, exceptfds, timeout, sigmask);
      break;
    }
    default:
      goto syscall_error;
  }

  /* libc reports host syscall failures as -1 plus errno, while the Linux
   * syscall ABI consumed by the enclave expects the negative errno value. */
  if (ret == -1)
    ret = -errno;

  /* Validate and publish the return slot before writing into shared memory. */
  uintptr_t ret_data_addr = edge_call_data_ptr();
  if (ret_data_addr == 0 ||
      edge_call_check_ptr_valid(ret_data_addr, sizeof(ret)) != 0 ||
      edge_call_setup_ret(edge_call, (void*)ret_data_addr, sizeof(ret)) != 0)
    goto syscall_error;
  memcpy((void*)ret_data_addr, &ret, sizeof(ret));
  edge_call->return_data.call_status = CALL_STATUS_OK;
  return;

syscall_error:
  edge_call->return_data.call_status = CALL_STATUS_SYSCALL_FAILED;
  edge_call->return_data.call_ret_offset = 0;
  edge_call->return_data.call_ret_size = 0;
}

#undef REQUIRE_FLEX
#undef REQUIRE_FIXED
