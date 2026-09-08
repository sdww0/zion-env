/* Minimal hello world for Zion enclave -- no libc or TLS dependency. */

#include "../hello-shared.h"

#define SBI_EXT_EXPERIMENTAL_ZION_ENCLAVE 0x08424b45
#define SBI_SM_EXIT_ENCLAVE 3006
#define RUNTIME_SYSCALL_OCALL 1001

static inline long host_print(const char *text, unsigned long length)
{
    register long a0 __asm__("a0") = HELLO_OCALL_PRINT;
    register const char *a1 __asm__("a1") = text;
    register unsigned long a2 __asm__("a2") = length;
    register long a3 __asm__("a3") = 0;
    register long a4 __asm__("a4") = 0;
    register long a7 __asm__("a7") = RUNTIME_SYSCALL_OCALL;
    __asm__ volatile("ecall"
                     : "+r"(a0)
                     : "r"(a1), "r"(a2), "r"(a3), "r"(a4), "r"(a7)
                     : "memory");
    return a0;
}

static inline void sbi_exit_enclave(long retval)
{
    register long a0 __asm__("a0") = retval;
    register long a6 __asm__("a6") = SBI_SM_EXIT_ENCLAVE;
    register long a7 __asm__("a7") = SBI_EXT_EXPERIMENTAL_ZION_ENCLAVE;
    __asm__ volatile("ecall" : "+r"(a0) : "r"(a6), "r"(a7) : "memory");
}

void _start(void)
{
    static const char message[] = "[ENCLAVE] hello, world!\n";
    long status = host_print(message, sizeof(message) - 1);

    sbi_exit_enclave(status == 0 ? 0 : 1);
}
