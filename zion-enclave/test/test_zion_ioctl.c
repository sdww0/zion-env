// SPDX-License-Identifier: Apache-2.0
/*
 * Zion Enclave Driver Test — public userspace ioctl test
 *
 * Tests the full enclave lifecycle through the kernel driver.
 *
 * Build:
 *   riscv64-unknown-linux-gnu-gcc -static -o test_zion_ioctl test_zion_ioctl.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <errno.h>

/* 内核头文件的 ioctl 定义 */
#define ZION_IOC_MAGIC 0xa4
#define ZION_IOC_CREATE_ENCLAVE   _IOR(ZION_IOC_MAGIC, 0x00, struct zion_ioctl_create_enclave)
#define ZION_IOC_DESTROY_ENCLAVE  _IOW(ZION_IOC_MAGIC, 0x01, struct zion_ioctl_create_enclave)
#define ZION_IOC_RUN_ENCLAVE      _IOR(ZION_IOC_MAGIC, 0x04, struct zion_ioctl_run_enclave)
#define ZION_IOC_RESUME_ENCLAVE   _IOR(ZION_IOC_MAGIC, 0x05, struct zion_ioctl_run_enclave)
#define ZION_IOC_FINALIZE_ENCLAVE _IOR(ZION_IOC_MAGIC, 0x06, struct zion_ioctl_create_enclave)
#define ZION_IOC_UTM_INIT         _IOR(ZION_IOC_MAGIC, 0x07, struct zion_ioctl_create_enclave)

struct zion_ioctl_create_enclave {
    uintptr_t eid;
    uintptr_t min_pages;
    uintptr_t utm_size;
    uintptr_t runtime_paddr;
    uintptr_t user_paddr;
    uintptr_t free_paddr;
    uintptr_t free_requested;
    uintptr_t epm_paddr;
    uintptr_t epm_size;
    uintptr_t utm_paddr;
};

struct zion_ioctl_run_enclave {
    uintptr_t eid;
    uintptr_t error;
    uintptr_t value;
};

#define PAGE_SIZE 4096
#define MIN_EPM_PAGES 16
#define UTM_SIZE (4 * PAGE_SIZE)

/* 颜色 */
#define RED   "\033[0;31m"
#define GREEN "\033[0;32m"
#define YELLOW "\033[1;33m"
#define NC    "\033[0m"

static int tests_run = 0, tests_passed = 0, tests_failed = 0;

#define PASS(name) do { printf(GREEN "  [PASS]" NC " %s\n", name); tests_passed++; tests_run++; } while(0)
#define FAIL(name, fmt, ...) do { printf(RED "  [FAIL]" NC " %s: " fmt "\n", name, ##__VA_ARGS__); tests_failed++; tests_run++; } while(0)

int main(void)
{
    int fd, ret;
    struct zion_ioctl_create_enclave create, utm_req, fin_req, destroy_req;
    struct zion_ioctl_run_enclave run_req;

    printf(YELLOW "=== Zion Enclave Driver Test ===" NC "\n\n");

    /* 1. Open device */
    printf(YELLOW "[Test 1] Open device" NC "\n");
    fd = open("/dev/zion_enclave", O_RDWR);
    if (fd < 0) {
        FAIL("open", "errno=%d (%s)", errno, strerror(errno));
        printf(RED "Cannot continue. Is zion_enclave.ko loaded?" NC "\n");
        return 1;
    }
    PASS("open(/dev/zion_enclave)");

    /* 2. Create enclave */
    printf(YELLOW "\n[Test 2] Create enclave" NC "\n");
    memset(&create, 0, sizeof(create));
    create.min_pages = MIN_EPM_PAGES;

    ret = ioctl(fd, ZION_IOC_CREATE_ENCLAVE, &create);
    if (ret != 0) {
        FAIL("ioctl(CREATE)", "ret=%d errno=%d (%s)", ret, errno, strerror(errno));
        close(fd);
        return 1;
    }
    PASS("ioctl(CREATE)");
    printf("  eid=%lu  epm_pa=0x%lx  epm_size=0x%lx (%lu pages)\n",
           (unsigned long)create.eid, (unsigned long)create.epm_paddr,
           (unsigned long)create.epm_size, (unsigned long)(create.epm_size / PAGE_SIZE));

    if (create.eid == 0) FAIL("eid", "eid=0");
    else PASS("eid allocated");

    if (create.epm_paddr == 0) FAIL("epm_paddr", "pa=0");
    else PASS("EPM PA returned");

    if (create.epm_size < MIN_EPM_PAGES * PAGE_SIZE)
        FAIL("epm_size", "size=0x%lx < 0x%lx", (unsigned long)create.epm_size, (unsigned long)(MIN_EPM_PAGES * PAGE_SIZE));
    else
        PASS("EPM size OK");

    /* 3. mmap EPM */
    printf(YELLOW "\n[Test 3] mmap EPM + write/readback" NC "\n");
    /* zion-ref driver: only single page mmap before finalize */
    unsigned long mmap_size = PAGE_SIZE < create.epm_size ? PAGE_SIZE : create.epm_size;
    void *epm = mmap(NULL, mmap_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (epm == MAP_FAILED) {
        FAIL("mmap(EPM)", "errno=%d (%s)", errno, strerror(errno));
    } else {
        PASS("mmap(EPM)");
        memset(epm, 0xAA, mmap_size);
        unsigned char *p = (unsigned char *)epm;
        int ok = 1;
        for (unsigned long i = 0; i < mmap_size; i++) {
            if (p[i] != 0xAA) { ok = 0; break; }
        }
        if (ok) PASS("EPM write/readback");
        else FAIL("EPM write/readback", "mismatch");
        memset(epm, 0, mmap_size);
        munmap(epm, mmap_size);
    }

    /* 4. UTM init */
    printf(YELLOW "\n[Test 4] UTM init" NC "\n");
    memset(&utm_req, 0, sizeof(utm_req));
    utm_req.eid = create.eid;
    utm_req.utm_size = UTM_SIZE;

    ret = ioctl(fd, ZION_IOC_UTM_INIT, &utm_req);
    if (ret != 0) {
        FAIL("ioctl(UTM_INIT)", "ret=%d errno=%d (%s)", ret, errno, strerror(errno));
    } else {
        PASS("ioctl(UTM_INIT)");
        printf("  utm_pa=0x%lx\n", (unsigned long)utm_req.utm_paddr);
    }

    /* 5. Finalize — 没有真实ELF, 预期失败 */
    printf(YELLOW "\n[Test 5] Finalize (expected fail without ELF)" NC "\n");
    memset(&fin_req, 0, sizeof(fin_req));
    fin_req.eid = create.eid;
    fin_req.runtime_paddr = create.epm_paddr;
    fin_req.user_paddr    = create.epm_paddr + 0x1000;
    fin_req.free_paddr    = create.epm_paddr + 0x2000;
    fin_req.free_requested = create.epm_size - 0x2000;

    ret = ioctl(fd, ZION_IOC_FINALIZE_ENCLAVE, &fin_req);
    if (ret == 0) {
        PASS("ioctl(FINALIZE) — SM accepted");
    } else {
        printf(YELLOW "  [EXPECTED]" NC " FINALIZE failed: ret=%d errno=%d (%s)\n",
               ret, errno, strerror(errno));
        printf("  (没有加载Eyrie runtime, 预期失败)\n");
        tests_run++;
    }

    /* 6. Run (only if finalize succeeded) */
    printf(YELLOW "\n[Test 6] Run enclave" NC "\n");
    if (ret == 0) {
        memset(&run_req, 0, sizeof(run_req));
        run_req.eid = create.eid;
        ret = ioctl(fd, ZION_IOC_RUN_ENCLAVE, &run_req);
        if (ret == 0) {
            if (run_req.error == 100008)
                PASS("ioctl(RUN) — edge call host");
            else if (run_req.error == 0)
                PASS("ioctl(RUN) — normal exit");
            else
                FAIL("ioctl(RUN)", "error=%lu", (unsigned long)run_req.error);
        } else {
            FAIL("ioctl(RUN)", "ret=%d errno=%d", ret, errno);
        }
    } else {
        printf("  [SKIP] 没有finalize的enclave\n");
    }

    /* 7. Destroy */
    printf(YELLOW "\n[Test 7] Destroy enclave" NC "\n");
    memset(&destroy_req, 0, sizeof(destroy_req));
    destroy_req.eid = create.eid;
    ret = ioctl(fd, ZION_IOC_DESTROY_ENCLAVE, &destroy_req);
    if (ret == 0) PASS("ioctl(DESTROY)");
    else FAIL("ioctl(DESTROY)", "ret=%d errno=%d", ret, errno);

    /* 8. Re-create after destroy */
    printf(YELLOW "\n[Test 8] Re-create after destroy" NC "\n");
    memset(&create, 0, sizeof(create));
    create.min_pages = MIN_EPM_PAGES;
    ret = ioctl(fd, ZION_IOC_CREATE_ENCLAVE, &create);
    if (ret == 0) {
        PASS("ioctl(CREATE) after destroy");
        memset(&destroy_req, 0, sizeof(destroy_req));
        destroy_req.eid = create.eid;
        ioctl(fd, ZION_IOC_DESTROY_ENCLAVE, &destroy_req);
    } else {
        FAIL("ioctl(CREATE) after destroy", "ret=%d errno=%d", ret, errno);
    }

    /* Summary */
    printf(YELLOW "\n=== Results ===" NC "\n");
    printf("  Total:  %d\n", tests_run);
    printf(GREEN "  Passed: %d\n" NC, tests_passed);
    if (tests_failed > 0)
        printf(RED "  Failed: %d\n" NC, tests_failed);
    else
        printf(GREEN "  Failed: 0\n" NC);

    close(fd);
    return tests_failed > 0 ? 1 : 0;
}
