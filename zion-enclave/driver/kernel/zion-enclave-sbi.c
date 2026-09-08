#include "zion-enclave-sbi.h"

struct sbiret sbi_sm_create_enclave(struct zion_sbi_create_t* args) {
  return sbi_ecall(SBI_EXT_EXPERIMENTAL_ZION_ENCLAVE,
      SBI_SM_CREATE_ENCLAVE,
      (unsigned long) args, 0, 0, 0, 0, 0);
}

struct sbiret sbi_sm_run_enclave(unsigned long eid) {
  return sbi_ecall(SBI_EXT_EXPERIMENTAL_ZION_ENCLAVE,
      SBI_SM_RUN_ENCLAVE,
      eid, 0, 0, 0, 0, 0);
}

struct sbiret sbi_sm_destroy_enclave(unsigned long eid) {
  return sbi_ecall(SBI_EXT_EXPERIMENTAL_ZION_ENCLAVE,
      SBI_SM_DESTROY_ENCLAVE,
      eid, 0, 0, 0, 0, 0);
}

struct sbiret sbi_sm_resume_enclave(unsigned long eid) {
  return sbi_ecall(SBI_EXT_EXPERIMENTAL_ZION_ENCLAVE,
      SBI_SM_RESUME_ENCLAVE,
      eid, 0, 0, 0, 0, 0);
}

struct sbiret sbi_sm_reserve_mem(unsigned long base, unsigned long pages) {
  return sbi_ecall(SBI_EXT_EXPERIMENTAL_ZION_ENCLAVE,
      SBI_SM_RESERVE_MEM,
      0, base, pages, 0, 0, 0);
}
