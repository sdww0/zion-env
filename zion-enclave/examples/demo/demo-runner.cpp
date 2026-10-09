#include "demo-shared.h"
#include "host/zion.h"
#include "edge/edge_call.h"
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <ctime>

static uint64_t expected = DEMO_SEED;
static uint64_t next_step = 1;
static bool failed;

static void progress_call(void *buffer)
{
    auto *call = static_cast<edge_call *>(buffer);
    uintptr_t address;
    size_t size;
    demo_progress progress;

    call->return_data.call_status = CALL_STATUS_ERROR;
    if (edge_call_args_ptr(call, &address, &size) || size != sizeof(progress)) {
        failed = true;
        return;
    }
    std::memcpy(&progress, reinterpret_cast<void *>(address), sizeof(progress));
    expected = demo_compute(expected);
    if (progress.step != next_step || progress.checksum != expected) {
        std::fprintf(stderr, "[ZION DEMO] FAIL: step/checksum mismatch\n");
        failed = true;
        return;
    }
    std::printf("[ZION ENCLAVE] COMPUTE %2lu/%u checksum=0x%016lx VERIFIED\n",
                static_cast<unsigned long>(progress.step), DEMO_STEPS,
                static_cast<unsigned long>(progress.checksum));
    /* Only the OCALL payload is changed; no arbitrary physical access. */
    std::memset(reinterpret_cast<void *>(address), 0xa5, sizeof(progress));
    std::printf("[CVM DEMO] shared payload overwritten; pause 1s, then resume enclave\n");
    std::fflush(stdout);
    timespec remaining = {1, 0};
    while (nanosleep(&remaining, &remaining) && errno == EINTR) {}
    next_step++;
    call->return_data.call_ret_offset = 0;
    call->return_data.call_ret_size = 0;
    call->return_data.call_status = CALL_STATUS_OK;
}

int main(int argc, char **argv)
{
    if (argc != 4) {
        std::fprintf(stderr, "Usage: %s <demo> <runtime> <loader>\n", argv[0]);
        return 2;
    }
    Zion::Enclave enclave;
    Zion::Params params;
    params.setFreeMemSize(256 * 1024);
    params.setUntrustedSize(256 * 1024);
    std::puts("[ZION DEMO] CREATE nested enclave; ten verified compute/OCALL rounds");
    if (enclave.init(argv[1], argv[2], argv[3], params) != Zion::Error::Success)
        return 1;
    enclave.registerOcallDispatch(incoming_call_dispatch);
    if (register_call(DEMO_OCALL_PROGRESS, progress_call))
        return 1;
    edge_call_init_internals(reinterpret_cast<uintptr_t>(enclave.getSharedBuffer()),
                             enclave.getSharedBufferSize());
    uintptr_t result = 1;
    const auto error = enclave.run(&result);
    const auto destroy_error = enclave.destroy();
    if (error != Zion::Error::Success || result || failed ||
        next_step != DEMO_STEPS + 1 || destroy_error != Zion::Error::Success) {
        std::fprintf(stderr, "[ZION DEMO] FAIL: computation or lifecycle\n");
        return 1;
    }
    std::puts("[ZION DEMO] PASS: private checksum continuity across 10 OCALLs; destroyed");
    std::puts("[ZION DEMO] LIMIT: not attestation or a private-page access-denial test");
    return 0;
}
