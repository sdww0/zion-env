//******************************************************************************
// Copyright (c) 2018, The Regents of the University of California (Regents).
// All Rights Reserved. See LICENSE for license details.
//------------------------------------------------------------------------------
#include "edge/edge_call.h"
#include "host/zion.h"
#include "../hello-shared.h"
#include <cstdio>

using namespace Zion;

static void print_enclave_message(void* buffer) {
  edge_call* call = static_cast<edge_call*>(buffer);
  uintptr_t message;
  size_t message_size;

  if (edge_call_args_ptr(call, &message, &message_size) != 0 ||
      message_size > 4096) {
    call->return_data.call_status = CALL_STATUS_BAD_OFFSET;
    return;
  }

  if (message_size != 0 &&
      std::fwrite(reinterpret_cast<void*>(message), 1, message_size, stdout) !=
          message_size) {
    call->return_data.call_status = CALL_STATUS_ERROR;
    return;
  }
  std::fflush(stdout);
  call->return_data.call_status = CALL_STATUS_OK;
  call->return_data.call_ret_offset = 0;
  call->return_data.call_ret_size = 0;
}

int
main(int argc, char** argv) {
  if (argc != 4) {
    fprintf(stderr, "Usage: %s <eapp> <runtime> <loader>\n", argv[0]);
    return 2;
  }

  Enclave enclave;
  Params params;

  params.setFreeMemSize(256 * 1024);
  params.setUntrustedSize(256 * 1024);

  Error error = enclave.init(argv[1], argv[2], argv[3], params);
  if (error != Error::Success) {
    fprintf(stderr, "[ZION] FAIL: hello enclave init (%d)\n",
            static_cast<int>(error));
    return 1;
  }

  enclave.registerOcallDispatch(incoming_call_dispatch);
  if (register_call(HELLO_OCALL_PRINT, print_enclave_message) != 0) {
    fprintf(stderr, "[ZION] FAIL: hello OCALL registration\n");
    return 1;
  }
  edge_call_init_internals(
      (uintptr_t)enclave.getSharedBuffer(), enclave.getSharedBufferSize());

  uintptr_t encl_ret = 0;
  error = enclave.run(&encl_ret);
  if (error != Error::Success) {
    fprintf(stderr, "[ZION] FAIL: hello enclave run (%d)\n",
            static_cast<int>(error));
    return 1;
  }

  printf("enclave returned: %lu\n", encl_ret);
  if (encl_ret != 0) {
    fprintf(stderr, "[ZION] FAIL: hello enclave returned %lu\n", encl_ret);
    return 1;
  }

  error = enclave.destroy();
  if (error != Error::Success) {
    fprintf(stderr, "[ZION] FAIL: hello enclave destroy (%d)\n",
            static_cast<int>(error));
    return 1;
  }

  printf("[ZION] PASS: hello enclave lifecycle\n");

  return 0;
}
