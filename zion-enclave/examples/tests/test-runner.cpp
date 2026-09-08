//******************************************************************************
// Copyright (c) 2018, The Regents of the University of California (Regents).
// All Rights Reserved. See LICENSE for license details.
//------------------------------------------------------------------------------
#include <getopt.h>
#include <cstdio>
#include <iostream>
#include "edge_wrapper.h"
#include "host/zion.h"
#include "verifier/report.h"
#include "verifier/test_dev_key.h"

const char* longstr = "hellohellohellohellohellohellohellohellohellohello";

unsigned long
print_buffer(char* str) {
  printf("Enclave said: %s", str);
  return strlen(str);
}

void
print_value(unsigned long val) {
  printf("Enclave said value: %lu\n", val);
  return;
}

const char*
get_host_string() {
  return longstr;
}

void
print_hex(void* buffer, size_t len) {
  size_t i;
  for (i = 0; i < len; i += sizeof(uintptr_t)) {
    printf("%.16lx ", *((uintptr_t*)((uintptr_t)buffer + i)));
  }
  printf("\n");
}

void
copy_report(void* buffer) {
  Report report;

  report.fromBytes((unsigned char*)buffer);

  if (report.checkSignaturesOnly(_sanctum_dev_public_key)) {
    printf("Attestation report SIGNATURE is valid\n");
  } else {
    printf("Attestation report is invalid\n");
  }
}

int
main(int argc, char** argv) {
  if (argc < 4) {
    printf(
        "Usage: %s <eapp> <runtime> <loader> [--utm-size SIZE(K)] [--freemem-size "
        "SIZE(K)] [--time] [--load-only] [--utm-ptr 0xPTR] [--retval EXPECTED]\n",
        argv[0]);
    return 2;
  }

  int self_timing = 0;
  int load_only   = 0;

  size_t untrusted_size = 2 * 1024 * 1024;
  size_t freemem_size   = 48 * 1024 * 1024;
  bool retval_exist = false;
  unsigned long retval = 0;

  static struct option long_options[] = {
      {"time", no_argument, &self_timing, 1},
      {"load-only", no_argument, &load_only, 1},
      {"utm-size", required_argument, 0, 'u'},
      {"freemem-size", required_argument, 0, 'f'},
      {"retval", required_argument, 0, 'r'},
      {0, 0, 0, 0}};

  char* eapp_file = argv[1];
  char* rt_file   = argv[2];
  char* ld_file   = argv[3];

  int c;
  int opt_index = 3;
  while (1) {
    c = getopt_long(argc, argv, "u:f:", long_options, &opt_index);

    if (c == -1) break;

    switch (c) {
      case 0:
        break;
      case 'u':
        untrusted_size = atoi(optarg) * 1024;
        break;
      case 'f':
        freemem_size = atoi(optarg) * 1024;
        break;
      case 'r':
        retval_exist = true;
        retval = atoi(optarg);
        break;
    }
  }

  Zion::Enclave enclave;
  Zion::Params params;
  unsigned long cycles1 = 0, cycles2 = 0, cycles3 = 0, cycles4 = 0;

  params.setFreeMemSize(freemem_size);
  params.setUntrustedSize(untrusted_size);

  if (self_timing) {
    asm volatile("rdcycle %0" : "=r"(cycles1));
  }

  Zion::Error error = enclave.init(eapp_file, rt_file, ld_file, params);
  if (error != Zion::Error::Success) {
    fprintf(stderr, "[ZION] FAIL: enclave init (%d)\n",
            static_cast<int>(error));
    return 1;
  }

  if (self_timing) {
    asm volatile("rdcycle %0" : "=r"(cycles2));
  }

  edge_init(&enclave);

  if (self_timing) {
    asm volatile("rdcycle %0" : "=r"(cycles3));
  }

  uintptr_t encl_ret = 0;
  if (!load_only) {
    error = enclave.run(&encl_ret);
    if (error != Zion::Error::Success) {
      fprintf(stderr, "[ZION] FAIL: enclave run (%d)\n",
              static_cast<int>(error));
      return 1;
    }
  }

  if (!load_only && retval_exist && encl_ret != retval) {
    fprintf(stderr,
            "[ZION] FAIL: enclave returned a wrong value (%lu != %lu)\n",
            static_cast<unsigned long>(encl_ret), retval);
    return 1;
  }

  if (self_timing) {
    asm volatile("rdcycle %0" : "=r"(cycles4));
    printf("[zion-test] Init: %lu cycles\r\n", cycles2 - cycles1);
    printf("[zion-test] Runtime: %lu cycles\r\n", cycles4 - cycles3);
  }

  error = enclave.destroy();
  if (error != Zion::Error::Success) {
    fprintf(stderr, "[ZION] FAIL: enclave destroy (%d)\n",
            static_cast<int>(error));
    return 1;
  }

  printf("[ZION] PASS: enclave lifecycle");
  if (!load_only) printf(" (retval=%lu)", static_cast<unsigned long>(encl_ret));
  printf("\n");

  return 0;
}
