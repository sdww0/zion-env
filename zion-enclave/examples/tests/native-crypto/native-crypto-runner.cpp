#include <cstdio>
#include <cstring>

#include "edge/edge_call.h"
#include "host/zion.h"
#include "verifier/ed25519/ed25519.h"
#include "verifier/test_dev_key.h"

#include "native-crypto-shared.h"

static native_crypto_result received;
static bool received_result;

struct native_crypto_run_args {
  Zion::Enclave* enclave;
  uintptr_t* enclave_retval;
  Zion::Error error;
};

extern "C" uintptr_t native_crypto_run(void* opaque) {
  native_crypto_run_args* args =
      static_cast<native_crypto_run_args*>(opaque);
  args->error = args->enclave->run(args->enclave_retval);
  return static_cast<uintptr_t>(args->error);
}

extern "C" uintptr_t native_crypto_fp_guard(
    void* opaque, uint64_t fp_pattern, uintptr_t fcsr_pattern,
    uint64_t* observed_fp, uintptr_t* observed_fcsr);

static const uint8_t long_ocall_return[NATIVE_CRYPTO_LONG_RETURN_SIZE] = {
    0x10, 0x21, 0x32, 0x43, 0x54, 0x65, 0x76, 0x87,
    0x98, 0xa9, 0xba, 0xcb, 0xdc, 0xed, 0xfe, 0x0f};

static bool verify_sm_signature(const native_crypto_report& report) {
  return ed25519_verify(report.sm.signature,
                        reinterpret_cast<const unsigned char*>(&report.sm),
                        NATIVE_CRYPTO_MDSIZE + NATIVE_CRYPTO_PUBLIC_KEY_SIZE,
                        _sanctum_dev_public_key) != 0;
}

static bool verify_enclave_signature(const native_crypto_report& report) {
  if (report.enclave.data_len > NATIVE_CRYPTO_ATTEST_DATA_MAXLEN)
    return false;
  return ed25519_verify(
             report.enclave.signature,
             reinterpret_cast<const unsigned char*>(&report.enclave),
             NATIVE_CRYPTO_MDSIZE + sizeof(report.enclave.data_len) +
                 report.enclave.data_len,
             report.sm.public_key) != 0;
}

static bool verify_sealing_signature(
    const native_crypto_sealing_key_v1& seal,
    const unsigned char sm_public_key[32]) {
  return ed25519_verify(
             seal.signature, reinterpret_cast<const unsigned char*>(&seal),
             NATIVE_CRYPTO_SEALING_V1_SIGNED_SIZE, sm_public_key) != 0;
}

static bool verify_legacy_sealing_signature(
    const native_crypto_sealing_key& seal,
    const unsigned char sm_public_key[32]) {
  return ed25519_verify(seal.signature, seal.key,
                        NATIVE_CRYPTO_SEALING_KEY_SIZE,
                        sm_public_key) != 0;
}

static void receive_result_wrapper(void* buffer) {
  edge_call* call = static_cast<edge_call*>(buffer);
  uintptr_t args = 0;
  size_t args_len = 0;

  if (edge_call_args_ptr(call, &args, &args_len) != 0 ||
      args_len != sizeof(received)) {
    call->return_data.call_status = CALL_STATUS_BAD_OFFSET;
    return;
  }
  std::memcpy(&received, reinterpret_cast<void*>(args), sizeof(received));
  received_result = true;
  call->return_data.call_status = CALL_STATUS_OK;
}

static void stale_status_wrapper(void*) {
  /* Intentionally leave the return metadata untouched. The runtime must have
     reset it before yielding to the untrusted host. */
}

static void invalid_return_wrapper(void* buffer) {
  edge_call* call = static_cast<edge_call*>(buffer);

  call->return_data.call_ret_offset = _shared_len - 4;
  call->return_data.call_ret_size = 8;
  call->return_data.call_status = CALL_STATUS_OK;
}

static void long_return_wrapper(void* buffer) {
  edge_call* call = static_cast<edge_call*>(buffer);
  uintptr_t return_ptr = edge_call_data_ptr();

  if (return_ptr == 0 ||
      edge_call_setup_ret(call, reinterpret_cast<void*>(return_ptr),
                          sizeof(long_ocall_return)) != 0) {
    call->return_data.call_status = CALL_STATUS_BAD_PTR;
    return;
  }
  std::memcpy(reinterpret_cast<void*>(return_ptr), long_ocall_return,
              sizeof(long_ocall_return));
  call->return_data.call_status = CALL_STATUS_OK;
}

static bool all_random_words_unique(const uint64_t words[4]) {
  for (unsigned int i = 0; i < 4; i++)
    for (unsigned int j = i + 1; j < 4; j++)
      if (words[i] == words[j])
        return false;
  return true;
}

#define NATIVE_CRYPTO_REQUIRE(condition, message)                             \
  do {                                                                        \
    if (!(condition)) {                                                       \
      std::fprintf(stderr, "[NATIVE CRYPTO] check failed: %s\n", message);   \
      return false;                                                           \
    }                                                                         \
  } while (0)

static bool check_payload(void) {
  const native_crypto_report& first = received.reports[0];
  const native_crypto_report& second = received.reports[1];

  NATIVE_CRYPTO_REQUIRE(
      received.magic == NATIVE_CRYPTO_MAGIC &&
          received.version == NATIVE_CRYPTO_VERSION && received.reserved == 0 &&
          received.status_padding[0] == 0 && received.status_padding[1] == 0,
      "payload header");
  NATIVE_CRYPTO_REQUIRE(
      received.ocall_null_data_status != 0 &&
          received.ocall_null_return_status != 0 &&
          received.ocall_oversize_data_status != 0 &&
          received.ocall_stale_status != 0 &&
          received.ocall_invalid_return_status != 0 &&
          received.ocall_unmapped_data_status != 0 &&
          received.ocall_unmapped_return_status != 0 &&
          received.sharedcopy_unmapped_status != 0 &&
          received.attest_unmapped_input_status != 0 &&
          received.attest_unmapped_output_status != 0 &&
          received.uaccess_status_padding == 0,
      "OCALL invalid-input and untrusted-return rejection");
  NATIVE_CRYPTO_REQUIRE(received.ocall_truncated_return_status == 0,
                        "OCALL bounded return status");
  NATIVE_CRYPTO_REQUIRE(
      std::memcmp(received.ocall_truncated_return, long_ocall_return,
                  sizeof(received.ocall_truncated_return)) == 0 &&
          received.ocall_canary == NATIVE_CRYPTO_OCALL_CANARY,
      "OCALL return truncation and destination canary");
  NATIVE_CRYPTO_REQUIRE(
      received.attest_status[0] == 0 && received.attest_status[1] == 0 &&
          received.attest_oversize_status != 0,
      "attestation statuses");
  for (unsigned int i = 0; i < 3; i++)
    NATIVE_CRYPTO_REQUIRE(received.seal_status[i] == 0, "sealing status");
  NATIVE_CRYPTO_REQUIRE(received.seal_oversize_status != 0,
                        "oversized sealing rejection");
  NATIVE_CRYPTO_REQUIRE(received.legacy_seal_status == 0,
                        "legacy sealing status");

  NATIVE_CRYPTO_REQUIRE(
      first.enclave.data_len == sizeof(native_crypto_nonce_a) &&
          second.enclave.data_len == sizeof(native_crypto_nonce_b) &&
          std::memcmp(first.enclave.data, native_crypto_nonce_a,
                      sizeof(native_crypto_nonce_a)) == 0 &&
          std::memcmp(second.enclave.data, native_crypto_nonce_b,
                      sizeof(native_crypto_nonce_b)) == 0,
      "nonce binding");
  NATIVE_CRYPTO_REQUIRE(
      std::memcmp(first.dev_public_key, _sanctum_dev_public_key,
                  NATIVE_CRYPTO_PUBLIC_KEY_SIZE) == 0 &&
          std::memcmp(second.dev_public_key, _sanctum_dev_public_key,
                      NATIVE_CRYPTO_PUBLIC_KEY_SIZE) == 0,
      "device public key");
  NATIVE_CRYPTO_REQUIRE(verify_sm_signature(first), "first SM signature");
  NATIVE_CRYPTO_REQUIRE(verify_sm_signature(second), "second SM signature");
  NATIVE_CRYPTO_REQUIRE(verify_enclave_signature(first),
                        "first enclave signature");
  NATIVE_CRYPTO_REQUIRE(verify_enclave_signature(second),
                        "second enclave signature");
  NATIVE_CRYPTO_REQUIRE(
      std::memcmp(first.enclave.hash, second.enclave.hash,
                  NATIVE_CRYPTO_MDSIZE) == 0 &&
          std::memcmp(&first.sm, &second.sm, sizeof(first.sm)) == 0,
      "measurement or SM report consistency");

  native_crypto_report tampered_report = first;
  tampered_report.enclave.data[0] ^= 0x80;
  NATIVE_CRYPTO_REQUIRE(!verify_enclave_signature(tampered_report),
                        "tampered nonce rejection");
  tampered_report = first;
  tampered_report.sm.hash[0] ^= 0x80;
  NATIVE_CRYPTO_REQUIRE(!verify_sm_signature(tampered_report),
                        "tampered SM report rejection");

  for (unsigned int i = 0; i < 3; i++) {
    NATIVE_CRYPTO_REQUIRE(
        received.seals[i].kdf_version ==
                NATIVE_CRYPTO_SEALING_KDF_VERSION &&
            received.seals[i].reserved == 0,
        "sealing metadata");
    NATIVE_CRYPTO_REQUIRE(
        verify_sealing_signature(received.seals[i], first.sm.public_key),
        "sealing signature");
  }
  NATIVE_CRYPTO_REQUIRE(
      std::memcmp(received.seals[0].key, received.seals[1].key,
                  NATIVE_CRYPTO_SEALING_KEY_SIZE) == 0,
      "same-identity sealing stability");
  NATIVE_CRYPTO_REQUIRE(
      std::memcmp(received.seals[0].key, received.seals[2].key,
                  NATIVE_CRYPTO_SEALING_KEY_SIZE) != 0,
      "different-identity sealing isolation");
  NATIVE_CRYPTO_REQUIRE(
      verify_legacy_sealing_signature(received.legacy_seal,
                                      first.sm.public_key),
      "legacy sealing signature");
  NATIVE_CRYPTO_REQUIRE(
      std::memcmp(received.legacy_seal.key, received.seals[0].key,
                  NATIVE_CRYPTO_SEALING_KEY_SIZE) == 0,
      "legacy/v1 sealing compatibility");

  native_crypto_sealing_key_v1 tampered_seal = received.seals[0];
  tampered_seal.kdf_version ^= 1;
  NATIVE_CRYPTO_REQUIRE(
      !verify_sealing_signature(tampered_seal, first.sm.public_key),
      "tampered sealing metadata rejection");

  NATIVE_CRYPTO_REQUIRE(all_random_words_unique(received.random_words),
                        "random uniqueness");
  NATIVE_CRYPTO_REQUIRE(
      received.fp_initial == 0 && received.fcsr_initial == 0,
      "fresh enclave floating-point state");
  NATIVE_CRYPTO_REQUIRE(
      received.fp_after_ocalls == NATIVE_CRYPTO_ENCLAVE_FP_PATTERN &&
          received.fcsr_after_ocalls == NATIVE_CRYPTO_ENCLAVE_FCSR_PATTERN,
      "enclave floating-point stop/resume persistence");
  return true;
}

#undef NATIVE_CRYPTO_REQUIRE

int main(int argc, char** argv) {
  if (argc != 4) {
    std::fprintf(stderr, "Usage: %s <eapp> <runtime> <loader>\n", argv[0]);
    return 2;
  }

  Zion::Enclave enclave;
  Zion::Params params;
  params.setUntrustedSize(2 * 1024 * 1024);
  params.setFreeMemSize(512 * 1024);

  Zion::Error error = enclave.init(argv[1], argv[2], argv[3], params);
  if (error != Zion::Error::Success) {
    std::fprintf(stderr, "[NATIVE CRYPTO] FAIL: enclave init (%d)\n",
                 static_cast<int>(error));
    return 1;
  }

  enclave.registerOcallDispatch(incoming_call_dispatch);
  if (register_call(NATIVE_CRYPTO_OCALL, receive_result_wrapper) != 0 ||
      register_call(NATIVE_CRYPTO_STALE_STATUS_OCALL,
                    stale_status_wrapper) != 0 ||
      register_call(NATIVE_CRYPTO_INVALID_RETURN_OCALL,
                    invalid_return_wrapper) != 0 ||
      register_call(NATIVE_CRYPTO_LONG_RETURN_OCALL,
                    long_return_wrapper) != 0) {
    std::fprintf(stderr, "[NATIVE CRYPTO] FAIL: OCALL registration\n");
    enclave.destroy();
    return 1;
  }
  edge_call_init_internals(
      reinterpret_cast<uintptr_t>(enclave.getSharedBuffer()),
      enclave.getSharedBufferSize());

  uintptr_t enclave_retval = 0;
  uint64_t observed_host_fp = 0;
  uintptr_t observed_host_fcsr = 0;
  native_crypto_run_args run_args = {&enclave, &enclave_retval,
                                     Zion::Error::Success};
  native_crypto_fp_guard(&run_args, NATIVE_CRYPTO_HOST_FP_PATTERN,
                         NATIVE_CRYPTO_HOST_FCSR_PATTERN, &observed_host_fp,
                         &observed_host_fcsr);
  error = run_args.error;
  if (error != Zion::Error::Success || enclave_retval != 0) {
    std::fprintf(stderr,
                 "[NATIVE CRYPTO] FAIL: enclave run (%d, retval=%lu)\n",
                 static_cast<int>(error),
                 static_cast<unsigned long>(enclave_retval));
    enclave.destroy();
    return 1;
  }
  if (observed_host_fp != NATIVE_CRYPTO_HOST_FP_PATTERN ||
      observed_host_fcsr != NATIVE_CRYPTO_HOST_FCSR_PATTERN) {
    std::fprintf(stderr,
                 "[NATIVE CRYPTO] FAIL: host FP context was corrupted\n");
    enclave.destroy();
    return 1;
  }

  error = enclave.destroy();
  if (error != Zion::Error::Success) {
    std::fprintf(stderr, "[NATIVE CRYPTO] FAIL: enclave destroy (%d)\n",
                 static_cast<int>(error));
    return 1;
  }
  if (!received_result || !check_payload()) {
    std::fprintf(stderr, "[NATIVE CRYPTO] FAIL: cryptographic validation\n");
    return 1;
  }

  std::printf("[NATIVE CRYPTO] PASS: uaccess faults, OCALL bounds, "
              "attestation nonce chain, legacy/v1 sealing, tamper rejection, "
              "random uniqueness, and FP context isolation verified\n");
  return 0;
}
