#define _GNU_SOURCE
#include <stddef.h>
#include <stdint.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/epoll.h>
#include <unistd.h>

#include "edge_call.h"
#include "edge_syscall.h"

#define BUFFER_LEN 1024U
#define GUARD_LEN 32U

static unsigned char shared_storage[BUFFER_LEN + GUARD_LEN]
    __attribute__((aligned(16)));
static int handler_called;

#define REQUIRE(condition, label)                                            \
  do {                                                                       \
    if (!(condition)) {                                                      \
      fprintf(stderr, "[EDGE CALL SELFTEST] FAIL: %s (line %d)\n", label,  \
              __LINE__);                                                     \
      return 1;                                                              \
    }                                                                        \
  } while (0)

static void
test_handler(void* buffer) {
  struct edge_call* call = (struct edge_call*)buffer;

  handler_called = 1;
  call->return_data.call_status = CALL_STATUS_OK;
}

static int
test_ranges(void) {
  uintptr_t ptr = 0;
  edge_data_offset offset = 0;
  uintptr_t start = (uintptr_t)shared_storage;

  edge_call_init_internals(start, BUFFER_LEN);
  REQUIRE(edge_call_get_ptr_from_offset(0, BUFFER_LEN, &ptr) == 0 &&
              ptr == start,
          "complete buffer range");
  REQUIRE(edge_call_get_ptr_from_offset(BUFFER_LEN, 0, &ptr) == 0 &&
              ptr == start + BUFFER_LEN,
          "zero-length end range");
  REQUIRE(edge_call_get_ptr_from_offset(BUFFER_LEN, 1, &ptr) != 0,
          "nonempty range at end rejected");
  REQUIRE(edge_call_get_ptr_from_offset(BUFFER_LEN + 1, 0, &ptr) != 0,
          "offset beyond end rejected");
  REQUIRE(edge_call_get_ptr_from_offset(0, BUFFER_LEN + 1, &ptr) != 0,
          "oversized range rejected");
  REQUIRE(edge_call_get_ptr_from_offset(0, 0, NULL) != 0,
          "null pointer output rejected");
  REQUIRE(edge_call_check_ptr_valid(start + BUFFER_LEN, 0) == 0,
          "zero-length end pointer");
  REQUIRE(edge_call_check_ptr_valid(start + BUFFER_LEN, 1) != 0,
          "end pointer with data rejected");
  REQUIRE(edge_call_check_ptr_valid(start - 1, 0) != 0,
          "pointer below buffer rejected");
  REQUIRE(edge_call_check_ptr_valid(start, SIZE_MAX) != 0,
          "overflowing length rejected");
  REQUIRE(edge_call_get_offset_from_ptr(start, 1, &offset) == 0 && offset == 0,
          "pointer to offset conversion");
  REQUIRE(edge_call_get_offset_from_ptr(start, 1, NULL) != 0,
          "null offset output rejected");

  edge_call_init_internals(UINTPTR_MAX - 7U, 16U);
  REQUIRE(edge_call_check_ptr_valid(UINTPTR_MAX - 7U, 0) != 0,
          "overflowing shared buffer rejected");
  REQUIRE(edge_call_get_ptr_from_offset(0, 0, &ptr) != 0,
          "overflowing shared buffer offset rejected");
  return 0;
}

static int
test_setup_atomicity(void) {
  struct edge_call* call = (struct edge_call*)shared_storage;
  uintptr_t start = (uintptr_t)shared_storage;

  memset(shared_storage, 0, sizeof(shared_storage));
  edge_call_init_internals(start, BUFFER_LEN);
  call->call_arg_offset = 17;
  call->call_arg_size = 19;
  REQUIRE(edge_call_setup_call(
              call, (void*)(start + BUFFER_LEN), 1) != 0,
          "invalid call setup rejected");
  REQUIRE(call->call_arg_offset == 17 && call->call_arg_size == 19,
          "failed call setup is atomic");

  call->return_data.call_ret_offset = 23;
  call->return_data.call_ret_size = 29;
  REQUIRE(edge_call_setup_ret(
              call, (void*)(start + BUFFER_LEN), 1) != 0,
          "invalid return setup rejected");
  REQUIRE(call->return_data.call_ret_offset == 23 &&
              call->return_data.call_ret_size == 29,
          "failed return setup is atomic");
  return 0;
}

static int
test_wrapped_return(void) {
  struct edge_call* call = (struct edge_call*)shared_storage;
  const size_t payload_capacity =
      BUFFER_LEN - sizeof(struct edge_call) - sizeof(struct edge_data);
  unsigned char payload[BUFFER_LEN];
  unsigned char snapshot[sizeof(shared_storage)];
  uintptr_t wrapper_ptr = 0;
  size_t wrapper_size = 0;
  struct edge_data wrapper;
  size_t i;

  for (i = 0; i < sizeof(payload); ++i) payload[i] = (unsigned char)i;
  memset(shared_storage, 0xA5, sizeof(shared_storage));
  memset(call, 0, sizeof(*call));
  edge_call_init_internals((uintptr_t)shared_storage, BUFFER_LEN);

  REQUIRE(edge_call_setup_wrapped_ret(
              call, payload, payload_capacity) == 0,
          "exact-fit wrapped return");
  REQUIRE(edge_call_ret_ptr(call, &wrapper_ptr, &wrapper_size) == 0 &&
              wrapper_size == sizeof(wrapper),
          "wrapped return descriptor range");
  memcpy(&wrapper, (void*)wrapper_ptr, sizeof(wrapper));
  REQUIRE(wrapper.offset == sizeof(struct edge_call) + sizeof(wrapper) &&
              wrapper.size == payload_capacity,
          "wrapped return descriptor values");
  REQUIRE(memcmp(shared_storage + wrapper.offset, payload, payload_capacity) ==
              0,
          "wrapped return payload");
  for (i = BUFFER_LEN; i < sizeof(shared_storage); ++i) {
    REQUIRE(shared_storage[i] == 0xA5, "wrapped return guard unchanged");
  }

  memcpy(snapshot, shared_storage, sizeof(snapshot));
  REQUIRE(edge_call_setup_wrapped_ret(
              call, payload, payload_capacity + 1) != 0,
          "oversized wrapped return rejected");
  REQUIRE(memcmp(snapshot, shared_storage, sizeof(snapshot)) == 0,
          "rejected wrapped return writes nothing");
  REQUIRE(edge_call_setup_wrapped_ret(call, NULL, 1) != 0,
          "null wrapped payload rejected");
  return 0;
}

static int
test_dispatch_bounds(void) {
  struct edge_call* call = (struct edge_call*)shared_storage;

  memset(shared_storage, 0, sizeof(shared_storage));
  edge_call_init_internals((uintptr_t)shared_storage, BUFFER_LEN);
  REQUIRE(register_call(MAX_EDGE_CALL, test_handler) != 0,
          "one-past call registration rejected");
  REQUIRE(register_call(MAX_EDGE_CALL - 1, test_handler) == 0,
          "last valid call registration");

  call->call_id = MAX_EDGE_CALL;
  call->return_data.call_status = CALL_STATUS_OK;
  incoming_call_dispatch(call);
  REQUIRE(call->return_data.call_status == CALL_STATUS_BAD_CALL_ID,
          "one-past dispatch rejected");

  handler_called = 0;
  call->call_id = MAX_EDGE_CALL - 1;
  call->return_data.call_status = CALL_STATUS_ERROR;
  incoming_call_dispatch(call);
  REQUIRE(handler_called &&
              call->return_data.call_status == CALL_STATUS_OK,
          "last valid dispatch succeeds");

  handler_called = 0;
  incoming_call_dispatch(shared_storage + BUFFER_LEN);
  REQUIRE(!handler_called, "out-of-range dispatch ignored safely");
  return 0;
}

static int
test_syscall_dispatch(void) {
  struct edge_call* call = (struct edge_call*)shared_storage;
  struct edge_syscall* request = (struct edge_syscall*)(
      shared_storage + sizeof(struct edge_call));
  uintptr_t return_ptr = 0;
  size_t return_size = 0;
  int64_t return_value = -1;

  memset(shared_storage, 0, sizeof(shared_storage));
  edge_call_init_internals((uintptr_t)shared_storage, BUFFER_LEN);

  call->call_id = EDGECALL_SYSCALL;
  REQUIRE(edge_call_setup_call(call, request, 0) == 0,
          "empty syscall request setup");
  incoming_call_dispatch(call);
  REQUIRE(call->return_data.call_status == CALL_STATUS_SYSCALL_FAILED,
          "empty syscall request rejected");

  memset(shared_storage, 0, sizeof(shared_storage));
  request->syscall_num = SYS_write;
  sargs_SYS_write* write_args = (sargs_SYS_write*)request->data;
  write_args->fd = -1;
  write_args->len = 1;
  call->call_id = EDGECALL_SYSCALL;
  REQUIRE(edge_call_setup_call(
              call, request,
              sizeof(*request) + sizeof(*write_args)) == 0,
          "short write syscall setup");
  incoming_call_dispatch(call);
  REQUIRE(call->return_data.call_status == CALL_STATUS_SYSCALL_FAILED,
          "short flexible syscall rejected");

  memset(shared_storage, 0, sizeof(shared_storage));
  request->syscall_num = SYS_openat;
  sargs_SYS_openat* openat_args = (sargs_SYS_openat*)request->data;
  openat_args->path[0] = 'b';
  openat_args->path[1] = 'a';
  openat_args->path[2] = 'd';
  call->call_id = EDGECALL_SYSCALL;
  REQUIRE(edge_call_setup_call(
              call, request,
              sizeof(*request) + sizeof(*openat_args) + 3) == 0,
          "unterminated path syscall setup");
  incoming_call_dispatch(call);
  REQUIRE(call->return_data.call_status == CALL_STATUS_SYSCALL_FAILED,
          "unterminated path rejected");

  memset(shared_storage, 0, sizeof(shared_storage));
  request->syscall_num = SYS_epoll_pwait;
  sargs_SYS_epoll_pwait* epoll_args =
      (sargs_SYS_epoll_pwait*)request->data;
  epoll_args->epfd = -1;
  epoll_args->maxevents = 2;
  call->call_id = EDGECALL_SYSCALL;
  REQUIRE(edge_call_setup_call(
              call, request,
              sizeof(*request) + sizeof(*epoll_args)) == 0,
          "oversized epoll syscall setup");
  incoming_call_dispatch(call);
  REQUIRE(call->return_data.call_status == CALL_STATUS_SYSCALL_FAILED,
          "oversized epoll event array rejected");

  memset(shared_storage, 0, sizeof(shared_storage));
  request->syscall_num = SYS_epoll_ctl;
  sargs_SYS_epoll_ctl* epoll_ctl_args =
      (sargs_SYS_epoll_ctl*)request->data;
  epoll_ctl_args->epfd = -1;
  epoll_ctl_args->op = EPOLL_CTL_ADD;
  epoll_ctl_args->fd = -1;
  epoll_ctl_args->event_is_null = 1;
  call->call_id = EDGECALL_SYSCALL;
  REQUIRE(edge_call_setup_call(
              call, request,
              sizeof(*request) + sizeof(*epoll_ctl_args)) == 0,
          "null epoll add event setup");
  incoming_call_dispatch(call);
  REQUIRE(call->return_data.call_status == CALL_STATUS_SYSCALL_FAILED,
          "null epoll add event rejected");

  memset(shared_storage, 0, sizeof(shared_storage));
  request->syscall_num = SYS_fcntl;
  sargs_SYS_fcntl* fcntl_args = (sargs_SYS_fcntl*)request->data;
  fcntl_args->fd = -1;
  fcntl_args->cmd = F_GETOWN_EX;
  fcntl_args->has_struct = 0;
  fcntl_args->arg[0] = UINTPTR_MAX;
  call->call_id = EDGECALL_SYSCALL;
  REQUIRE(edge_call_setup_call(
              call, request,
              sizeof(*request) + sizeof(*fcntl_args) +
                  sizeof(fcntl_args->arg[0])) == 0,
          "pointer-valued fcntl command setup");
  incoming_call_dispatch(call);
  REQUIRE(call->return_data.call_status == CALL_STATUS_SYSCALL_FAILED,
          "pointer-valued fcntl command rejected");

  memset(shared_storage, 0, sizeof(shared_storage));
  request->syscall_num = SYS_pselect6;
  sargs_SYS_pselect* pselect_args = (sargs_SYS_pselect*)request->data;
  pselect_args->nfds = 0;
  pselect_args->readfds_is_null = 1;
  pselect_args->writefds_is_null = 1;
  pselect_args->exceptfds_is_null = 1;
  pselect_args->timeout_is_null = 1;
  pselect_args->sigmask_is_null = 0;
  pselect_args->sigsetsize = sizeof(uint64_t) - 1;
  call->call_id = EDGECALL_SYSCALL;
  REQUIRE(edge_call_setup_call(
              call, request,
              sizeof(*request) + sizeof(*pselect_args)) == 0,
          "invalid pselect sigset size setup");
  incoming_call_dispatch(call);
  REQUIRE(call->return_data.call_status == CALL_STATUS_SYSCALL_FAILED,
          "invalid pselect sigset size rejected");

  memset(shared_storage, 0, sizeof(shared_storage));
  request->syscall_num = SYS_socket;
  sargs_SYS_socket* socket_args = (sargs_SYS_socket*)request->data;
  socket_args->domain = -1;
  socket_args->type = SOCK_DGRAM;
  call->call_id = EDGECALL_SYSCALL;
  REQUIRE(edge_call_setup_call(
              call, request,
              sizeof(*request) + sizeof(*socket_args)) == 0,
          "failing host syscall setup");
  incoming_call_dispatch(call);
  REQUIRE(call->return_data.call_status == CALL_STATUS_OK &&
              edge_call_ret_ptr(call, &return_ptr, &return_size) == 0 &&
              return_size == sizeof(return_value),
          "failing host syscall return range");
  memcpy(&return_value, (void*)return_ptr, sizeof(return_value));
  REQUIRE(return_value == -EAFNOSUPPORT,
          "host errno translated to syscall ABI");

  memset(shared_storage, 0, sizeof(shared_storage));
  request->syscall_num = SYS_getcwd;
  sargs_SYS_getcwd* getcwd_args = (sargs_SYS_getcwd*)request->data;
  getcwd_args->size = 128;
  call->call_id = EDGECALL_SYSCALL;
  REQUIRE(edge_call_setup_call(
              call, request,
              sizeof(*request) + sizeof(*getcwd_args) + getcwd_args->size) ==
              0,
          "getcwd syscall setup");
  incoming_call_dispatch(call);
  REQUIRE(call->return_data.call_status == CALL_STATUS_OK &&
              edge_call_ret_ptr(call, &return_ptr, &return_size) == 0 &&
              return_size == sizeof(return_value),
          "getcwd syscall return range");
  memcpy(&return_value, (void*)return_ptr, sizeof(return_value));
  REQUIRE(return_value > 1 && return_value <= (int64_t)getcwd_args->size &&
              getcwd_args->buf[return_value - 1] == '\0' &&
              strlen(getcwd_args->buf) + 1 == (size_t)return_value,
          "getcwd return length matches terminated payload");

  memset(shared_storage, 0, sizeof(shared_storage));
  request->syscall_num = SYS_getuid;
  call->call_id = EDGECALL_SYSCALL;
  REQUIRE(edge_call_setup_call(call, request, sizeof(*request)) == 0,
          "valid syscall setup");
  incoming_call_dispatch(call);
  REQUIRE(call->return_data.call_status == CALL_STATUS_OK &&
              edge_call_ret_ptr(call, &return_ptr, &return_size) == 0 &&
              return_size == sizeof(return_value),
          "valid syscall return range");
  memcpy(&return_value, (void*)return_ptr, sizeof(return_value));
  REQUIRE(return_value == (int64_t)getuid(), "valid syscall return value");
  return 0;
}

int
main(void) {
  if (test_ranges() != 0 || test_setup_atomicity() != 0 ||
      test_wrapped_return() != 0 || test_dispatch_bounds() != 0 ||
      test_syscall_dispatch() != 0) {
    return 1;
  }

  puts("[EDGE CALL SELFTEST] PASS: ranges, overflow, wrapped returns, "
       "dispatch bounds, and syscall payloads verified");
  return 0;
}
