#ifndef ZION_DEMO_SHARED_H
#define ZION_DEMO_SHARED_H

#include <stdint.h>

#define DEMO_OCALL_PROGRESS 0
#define DEMO_STEPS 10
#define DEMO_ITERATIONS 100000
#define DEMO_SEED UINT64_C(0x5a494f4e12345678)

struct demo_progress {
    uint64_t step;
    uint64_t checksum;
};

static inline uint64_t demo_compute(uint64_t state)
{
    for (unsigned int i = 0; i < DEMO_ITERATIONS; i++) {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
    }
    return state;
}

#endif
