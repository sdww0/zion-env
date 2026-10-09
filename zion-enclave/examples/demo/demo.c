#include "demo-shared.h"
#include "syscall.h"

void _start(void)
{
    struct demo_progress progress;
    uint64_t private_state = DEMO_SEED;
    long result = 0;

    for (unsigned int step = 1; step <= DEMO_STEPS; step++) {
        private_state = demo_compute(private_state);
        progress.step = step;
        progress.checksum = private_state;
        if (ocall(DEMO_OCALL_PROGRESS, &progress, sizeof(progress), 0, 0)) {
            result = 1;
            break;
        }
    }

    register long a0 __asm__("a0") = result;
    register long a6 __asm__("a6") = 3006;
    register long a7 __asm__("a7") = 0x08424b45;
    __asm__ volatile("ecall" : "+r"(a0) : "r"(a6), "r"(a7) : "memory");
    for (;;)
        __asm__ volatile("nop");
}
