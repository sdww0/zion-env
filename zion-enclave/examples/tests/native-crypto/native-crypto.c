#include "app/eapp_utils.h"
#include "app/sealing.h"
#include "app/string.h"
#include "app/syscall.h"

#include "native-crypto-shared.h"

static struct native_crypto_result result;
static struct native_crypto_report rejected_report;

static uint64_t read_fs0(void) {
  uint64_t value;
  __asm__ volatile("fmv.x.d %0, fs0" : "=r"(value));
  return value;
}

static void write_fs0(uint64_t value) {
  __asm__ volatile("fmv.d.x fs0, %0" : : "r"(value));
}

static uint64_t read_fcsr(void) {
  uint64_t value;
  __asm__ volatile("frcsr %0" : "=r"(value));
  return value;
}

static void write_fcsr(uint64_t value) {
  __asm__ volatile("fscsr %0" : : "r"(value));
}

struct bounded_ocall_return {
  uint8_t data[NATIVE_CRYPTO_TRUNCATED_RETURN_SIZE];
  uint64_t canary;
};

void EAPP_ENTRY eapp_entry(void) {
  struct bounded_ocall_return bounded_return;
  uint64_t invalid_return = 0;
  uint8_t one_byte = 0x5a;
  void* unmapped = (void*)(uintptr_t)NATIVE_CRYPTO_UNMAPPED_PTR;
  unsigned int i;
  int ocall_status;

  memset(&result, 0, sizeof(result));
  memset(&rejected_report, 0, sizeof(rejected_report));
  result.magic = NATIVE_CRYPTO_MAGIC;
  result.version = NATIVE_CRYPTO_VERSION;
  result.fp_initial = read_fs0();
  result.fcsr_initial = read_fcsr();
  write_fs0(NATIVE_CRYPTO_ENCLAVE_FP_PATTERN);
  write_fcsr(NATIVE_CRYPTO_ENCLAVE_FCSR_PATTERN);

  memset(&bounded_return, 0, sizeof(bounded_return));
  bounded_return.canary = NATIVE_CRYPTO_OCALL_CANARY;
  result.ocall_null_data_status =
      ocall(NATIVE_CRYPTO_OCALL, 0, 1, 0, 0);
  result.ocall_null_return_status =
      ocall(NATIVE_CRYPTO_OCALL, &one_byte, sizeof(one_byte), 0, 1);
  result.ocall_oversize_data_status =
      ocall(NATIVE_CRYPTO_OCALL, &one_byte, NATIVE_CRYPTO_UTM_SIZE, 0, 0);
  result.ocall_stale_status =
      ocall(NATIVE_CRYPTO_STALE_STATUS_OCALL, 0, 0, 0, 0);
  result.ocall_invalid_return_status = ocall(
      NATIVE_CRYPTO_INVALID_RETURN_OCALL, 0, 0, &invalid_return,
      sizeof(invalid_return));
  result.ocall_truncated_return_status = ocall(
      NATIVE_CRYPTO_LONG_RETURN_OCALL, 0, 0, bounded_return.data,
      sizeof(bounded_return.data));
  result.ocall_unmapped_data_status =
      ocall(NATIVE_CRYPTO_OCALL, unmapped, 1, 0, 0);
  result.ocall_unmapped_return_status = ocall(
      NATIVE_CRYPTO_LONG_RETURN_OCALL, 0, 0, unmapped,
      NATIVE_CRYPTO_TRUNCATED_RETURN_SIZE);
  result.sharedcopy_unmapped_status = copy_from_shared(unmapped, 0, 1);
  result.attest_unmapped_input_status =
      attest_enclave(&rejected_report, unmapped, 1);
  result.attest_unmapped_output_status = attest_enclave(
      unmapped, (void *)native_crypto_nonce_a, sizeof(native_crypto_nonce_a));
  memcpy(result.ocall_truncated_return, bounded_return.data,
         sizeof(result.ocall_truncated_return));
  result.ocall_canary = bounded_return.canary;

  result.attest_status[0] = attest_enclave(
      &result.reports[0], (void *)native_crypto_nonce_a,
      sizeof(native_crypto_nonce_a));
  result.attest_status[1] = attest_enclave(
      &result.reports[1], (void *)native_crypto_nonce_b,
      sizeof(native_crypto_nonce_b));
  result.attest_oversize_status = attest_enclave(
      &rejected_report, (void *)native_crypto_nonce_a,
      NATIVE_CRYPTO_ATTEST_DATA_MAXLEN + 1);

  result.seal_status[0] = get_sealing_key_v1(
      (struct sealing_key_v1 *)&result.seals[0], sizeof(result.seals[0]),
      (void *)native_crypto_ident_a, sizeof(native_crypto_ident_a));
  result.seal_status[1] = get_sealing_key_v1(
      (struct sealing_key_v1 *)&result.seals[1], sizeof(result.seals[1]),
      (void *)native_crypto_ident_a, sizeof(native_crypto_ident_a));
  result.seal_status[2] = get_sealing_key_v1(
      (struct sealing_key_v1 *)&result.seals[2], sizeof(result.seals[2]),
      (void *)native_crypto_ident_b, sizeof(native_crypto_ident_b));
  result.legacy_seal_status = get_sealing_key(
      (struct sealing_key *)&result.legacy_seal, sizeof(result.legacy_seal),
      (void *)native_crypto_ident_a, sizeof(native_crypto_ident_a));
  result.seal_oversize_status = get_sealing_key_v1(
      (struct sealing_key_v1 *)&result.seals[2], sizeof(result.seals[2]),
      (void *)native_crypto_ident_b, NATIVE_CRYPTO_ATTEST_DATA_MAXLEN + 1);

  for (i = 0; i < 4; i++)
    result.random_words[i] = get_random_word();

  /* Every operation above crosses the SM through either an OCALL or a checked
   * enclave service. The enclave-owned FP/FCSR state must survive all stops
   * and resumes without inheriting the host guard pattern. */
  result.fp_after_ocalls = read_fs0();
  result.fcsr_after_ocalls = read_fcsr();

  ocall_status = ocall(NATIVE_CRYPTO_OCALL, &result, sizeof(result), 0, 0);
  EAPP_RETURN(ocall_status ? 1 : 0);
}
