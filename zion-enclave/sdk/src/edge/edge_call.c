//******************************************************************************
// Copyright (c) 2018, The Regents of the University of California (Regents).
// All Rights Reserved. See LICENSE for license details.
//------------------------------------------------------------------------------
#include <edge_call.h>
#include "string.h"
#include <sys/epoll.h>
#include <sys/socket.h>

uintptr_t _shared_start;
size_t _shared_len;

static int
edge_call_buffer_end(uintptr_t* end) {
  if (end == NULL || _shared_len > UINTPTR_MAX - _shared_start) {
    return -1;
  }

  *end = _shared_start + _shared_len;
  return 0;
}

void
edge_call_init_internals(uintptr_t buffer_start, size_t buffer_len) {
  _shared_start = buffer_start;
  _shared_len   = buffer_len;
}

int
edge_call_get_ptr_from_offset(
    edge_data_offset offset, size_t data_len, uintptr_t* ptr) {
  uintptr_t shared_end;

  if (ptr == NULL || edge_call_buffer_end(&shared_end) != 0 ||
      offset > _shared_len || data_len > _shared_len - offset) {
    return -1;
  }

  (void)shared_end;
  *ptr = _shared_start + offset;
  return 0;
}

int
edge_call_check_ptr_valid(uintptr_t ptr, size_t data_len) {
  uintptr_t shared_end;

  if (edge_call_buffer_end(&shared_end) != 0 || ptr < _shared_start ||
      ptr > shared_end) {
    return 1;
  }

  if (data_len > UINTPTR_MAX - ptr) {
    return 2;
  }

  if (data_len > shared_end - ptr) {
    return 3;
  }

  return 0;
}

int
edge_call_get_offset_from_ptr(
    uintptr_t ptr, size_t data_len, edge_data_offset* offset) {
  if (offset == NULL) return -1;

  int valid = edge_call_check_ptr_valid(ptr, data_len);
  if (valid != 0) return valid;

  /* ptr looks valid, create it */
  *offset = ptr - _shared_start;
  return 0;
}

int
edge_call_args_ptr(struct edge_call* edge_call, uintptr_t* ptr, size_t* size) {
  if (edge_call == NULL || ptr == NULL || size == NULL) return -1;

  *size = edge_call->call_arg_size;
  return edge_call_get_ptr_from_offset(edge_call->call_arg_offset, *size, ptr);
}

int
edge_call_ret_ptr(struct edge_call* edge_call, uintptr_t* ptr, size_t* size) {
  if (edge_call == NULL || ptr == NULL || size == NULL) return -1;

  *size = edge_call->return_data.call_ret_size;
  return edge_call_get_ptr_from_offset(
      edge_call->return_data.call_ret_offset, *size, ptr);
}

int
edge_call_setup_call(struct edge_call* edge_call, void* ptr, size_t size) {
  edge_data_offset offset;

  if (edge_call == NULL ||
      edge_call_get_offset_from_ptr((uintptr_t)ptr, size, &offset) != 0) {
    return -1;
  }

  edge_call->call_arg_size = size;
  edge_call->call_arg_offset = offset;
  return 0;
}

int
edge_call_setup_ret(struct edge_call* edge_call, void* ptr, size_t size) {
  edge_data_offset offset;

  if (edge_call == NULL ||
      edge_call_get_offset_from_ptr((uintptr_t)ptr, size, &offset) != 0) {
    return -1;
  }

  edge_call->return_data.call_ret_size = size;
  edge_call->return_data.call_ret_offset = offset;
  return 0;
}

/* This is only usable for the host */
int
edge_call_setup_wrapped_ret(
    struct edge_call* edge_call, void* ptr, size_t size) {
  const edge_data_offset wrapper_offset = sizeof(struct edge_call);
  const edge_data_offset payload_offset =
      sizeof(struct edge_call) + sizeof(struct edge_data);
  struct edge_data data_wrapper;
  uintptr_t wrapper_ptr;
  uintptr_t payload_ptr;

  if (edge_call == NULL || (size != 0 && ptr == NULL) ||
      edge_call_get_ptr_from_offset(
          wrapper_offset, sizeof(struct edge_data), &wrapper_ptr) != 0 ||
      edge_call_get_ptr_from_offset(payload_offset, size, &payload_ptr) != 0) {
    return -1;
  }

  data_wrapper.offset = payload_offset;
  data_wrapper.size = size;
  if (size != 0) memcpy((void*)payload_ptr, ptr, size);
  memcpy((void*)wrapper_ptr, &data_wrapper, sizeof(data_wrapper));

  return edge_call_setup_ret(
      edge_call, (void*)wrapper_ptr, sizeof(struct edge_data));
}

/* This is temporary until we have a better way to handle multiple things */
uintptr_t
edge_call_data_ptr() {
  uintptr_t ptr;

  if (edge_call_get_ptr_from_offset(
          sizeof(struct edge_call), 0, &ptr) != 0) {
    return 0;
  }
  return ptr;
}
