#ifndef NATIVE_CRYPTO_SHARED_H
#define NATIVE_CRYPTO_SHARED_H

#include <stddef.h>
#include <stdint.h>

#define NATIVE_CRYPTO_OCALL 5
#define NATIVE_CRYPTO_STALE_STATUS_OCALL 6
#define NATIVE_CRYPTO_INVALID_RETURN_OCALL 7
#define NATIVE_CRYPTO_LONG_RETURN_OCALL 8
#define NATIVE_CRYPTO_MAGIC UINT64_C(0x5a494f4e43525950)
#define NATIVE_CRYPTO_VERSION 4
#define NATIVE_CRYPTO_UTM_SIZE (2U * 1024U * 1024U)
#define NATIVE_CRYPTO_TRUNCATED_RETURN_SIZE 8
#define NATIVE_CRYPTO_LONG_RETURN_SIZE 16
#define NATIVE_CRYPTO_OCALL_CANARY UINT64_C(0x43414e4152595a31)
#define NATIVE_CRYPTO_UNMAPPED_PTR UINT64_C(0x0000000100000000)
#define NATIVE_CRYPTO_ATTEST_DATA_MAXLEN 1024
#define NATIVE_CRYPTO_MDSIZE 64
#define NATIVE_CRYPTO_PUBLIC_KEY_SIZE 32
#define NATIVE_CRYPTO_SIGNATURE_SIZE 64
#define NATIVE_CRYPTO_SEALING_KEY_SIZE 128
#define NATIVE_CRYPTO_SEALING_KDF_VERSION 1
#define NATIVE_CRYPTO_SEALING_V1_SIGNED_SIZE 136
#define NATIVE_CRYPTO_REPORT_SIZE 1352
#define NATIVE_CRYPTO_HOST_FP_PATTERN UINT64_C(0x1122334455667788)
#define NATIVE_CRYPTO_ENCLAVE_FP_PATTERN UINT64_C(0x8877665544332211)
#define NATIVE_CRYPTO_HOST_FCSR_PATTERN UINT64_C(0x40)
#define NATIVE_CRYPTO_ENCLAVE_FCSR_PATTERN UINT64_C(0x60)

static const uint8_t native_crypto_nonce_a[] = {
  'z', 'i', 'o', 'n', '-', 'n', 'a', 't', 'i', 'v', 'e', '-', 'a', 0x01
};
static const uint8_t native_crypto_nonce_b[] = {
  'z', 'i', 'o', 'n', '-', 'n', 'a', 't', 'i', 'v', 'e', '-', 'b', 0x02
};
static const uint8_t native_crypto_ident_a[] = {
  'n', 'a', 't', 'i', 'v', 'e', '-', 's', 'e', 'a', 'l', '-', 'a'
};
static const uint8_t native_crypto_ident_b[] = {
  'n', 'a', 't', 'i', 'v', 'e', '-', 's', 'e', 'a', 'l', '-', 'b'
};

struct native_crypto_enclave_report {
  uint8_t hash[NATIVE_CRYPTO_MDSIZE];
  uint64_t data_len;
  uint8_t data[NATIVE_CRYPTO_ATTEST_DATA_MAXLEN];
  uint8_t signature[NATIVE_CRYPTO_SIGNATURE_SIZE];
};

struct native_crypto_sm_report {
  uint8_t hash[NATIVE_CRYPTO_MDSIZE];
  uint8_t public_key[NATIVE_CRYPTO_PUBLIC_KEY_SIZE];
  uint8_t signature[NATIVE_CRYPTO_SIGNATURE_SIZE];
};

struct native_crypto_report {
  struct native_crypto_enclave_report enclave;
  struct native_crypto_sm_report sm;
  uint8_t dev_public_key[NATIVE_CRYPTO_PUBLIC_KEY_SIZE];
};

struct native_crypto_sealing_key_v1 {
  uint8_t key[NATIVE_CRYPTO_SEALING_KEY_SIZE];
  uint32_t kdf_version;
  uint32_t reserved;
  uint8_t signature[NATIVE_CRYPTO_SIGNATURE_SIZE];
};

struct native_crypto_sealing_key {
  uint8_t key[NATIVE_CRYPTO_SEALING_KEY_SIZE];
  uint8_t signature[NATIVE_CRYPTO_SIGNATURE_SIZE];
};

struct native_crypto_result {
  uint64_t magic;
  uint32_t version;
  uint32_t reserved;
  int32_t attest_status[2];
  int32_t attest_oversize_status;
  int32_t seal_status[3];
  int32_t seal_oversize_status;
  int32_t legacy_seal_status;
  uint32_t status_padding[2];
  int32_t ocall_null_data_status;
  int32_t ocall_null_return_status;
  int32_t ocall_oversize_data_status;
  int32_t ocall_stale_status;
  int32_t ocall_invalid_return_status;
  int32_t ocall_truncated_return_status;
  int32_t ocall_unmapped_data_status;
  int32_t ocall_unmapped_return_status;
  int32_t sharedcopy_unmapped_status;
  int32_t attest_unmapped_input_status;
  int32_t attest_unmapped_output_status;
  uint32_t uaccess_status_padding;
  uint8_t ocall_truncated_return[NATIVE_CRYPTO_TRUNCATED_RETURN_SIZE];
  uint64_t ocall_canary;
  uint64_t random_words[4];
  uint64_t fp_initial;
  uint64_t fp_after_ocalls;
  uint64_t fcsr_initial;
  uint64_t fcsr_after_ocalls;
  struct native_crypto_report reports[2];
  struct native_crypto_sealing_key_v1 seals[3];
  struct native_crypto_sealing_key legacy_seal;
};

#if defined(__cplusplus)
static_assert(sizeof(native_crypto_report) == NATIVE_CRYPTO_REPORT_SIZE,
              "native attestation report ABI mismatch");
static_assert(sizeof(native_crypto_sealing_key_v1) == 200,
              "native sealing v1 ABI mismatch");
static_assert(sizeof(native_crypto_sealing_key) == 192,
              "native legacy sealing ABI mismatch");
#else
_Static_assert(sizeof(struct native_crypto_report) == NATIVE_CRYPTO_REPORT_SIZE,
               "native attestation report ABI mismatch");
_Static_assert(sizeof(struct native_crypto_sealing_key_v1) == 200,
               "native sealing v1 ABI mismatch");
_Static_assert(sizeof(struct native_crypto_sealing_key) == 192,
               "native legacy sealing ABI mismatch");
#endif

#endif
