// SPDX-License-Identifier: GPL-2.0-only
/* CVM-side regression for nested enclave lifecycle and attestation calls. */
#include <asm/sbi.h>
#include <asm/delay.h>
#include <asm/fpu.h>
#include <asm/timex.h>
#include <crypto/hash.h>
#include <linux/completion.h>
#include <linux/cpu.h>
#include <linux/err.h>
#include <linux/gfp.h>
#include <linux/init.h>
#include <linux/irqflags.h>
#include <linux/kthread.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/string.h>

#include "cvm-ed25519.h"

#define ZION_SBI_EXT                    0x08424b45
#define SBI_SM_CREATE_ENCLAVE           2001
#define SBI_SM_DESTROY_ENCLAVE          2002
#define SBI_SM_RUN_ENCLAVE              2003
#define SBI_SM_RESUME_ENCLAVE           2005
#define SBI_SM_RANDOM                   3001
#define SBI_SM_CALL_PLUGIN              4000
#define SBI_SM_CREATE_CVM               1015
#define SBI_SM_LOAD_MEM                 1025
#define SBI_SM_REGISTER_PT              1021

#define SM_ENCLAVE_INTERRUPTED           100002
#define SM_ENCLAVE_NOT_RUNNABLE          100004
#define SM_ENCLAVE_NOT_DESTROYABLE      100005
#define SM_ENCLAVE_ILLEGAL_ARGUMENT      100008
#define SM_ENCLAVE_NOT_RESUMABLE         100010
#define SM_ENCLAVE_SBI_PROHIBITED        100014
#define SM_ENCLAVE_NOT_FRESH             100016

#define TEST_EPM_ORDER                  9
#define TEST_EPM_SIZE                   (PAGE_SIZE << TEST_EPM_ORDER)
#define TEST_EXIT_VALUE                 0x321
/*
 * Keep the enclave active across at least one 250 Hz guest timer tick even
 * when it executes directly on a fast Megrez hart.  The previous 250,000
 * iterations could finish before the next 4 ms tick, making the cross-CPU
 * resume test depend on where execution happened to land in the tick period.
 * Keep this value aligned to 4 KiB so the emitted LUI/ADDI pair represents it
 * without an immediate carry adjustment.
 */
#define TEST_BUSY_LOOPS                 16777216
#define TEST_MAX_RESUMES                128
#define TEST_HANDLE_SLOT_MASK            0xffUL
#define TEST_VARIANT_OFFSET             512
#define TEST_ATTEST_DATA_SIZE           sizeof(unsigned long)
#define TEST_ATTEST_MAXLEN              1024
#define TEST_REPORT_OFFSET              0
#define TEST_FRESH_REPORT_OFFSET        1360
#define TEST_SEAL1_OFFSET               2720
#define TEST_SEAL2_OFFSET               2920
#define TEST_SEAL3_OFFSET               3120
#define TEST_IDENT_OFFSET               3320
#define TEST_FRESH_IDENT_OFFSET         3328
#define TEST_ATTEST_ERROR_OFFSET        3336
#define TEST_SEAL_ERROR_OFFSET          3344
#define TEST_LEGACY_SEAL_OFFSET         3360
#define TEST_LEGACY_GUARD_OFFSET        3552
#define TEST_RANDOM1_OFFSET             3560
#define TEST_RANDOM2_OFFSET             3568
#define TEST_HOST_FID_ERROR_OFFSET      3576
#define TEST_FP_INITIAL_OFFSET          3584
#define TEST_FP_AFTER_TIMER_OFFSET      3592
#define TEST_FP_AFTER_SERVICES_OFFSET   3600
#define TEST_FCSR_INITIAL_OFFSET        3608
#define TEST_FCSR_AFTER_TIMER_OFFSET    3616
#define TEST_FCSR_AFTER_SERVICES_OFFSET 3624
#define TEST_FP_PATTERN_BASE            0x55667000U
#define TEST_FCSR_PATTERN_BASE          0x60U
#define TEST_PARENT_FP_PATTERN_BASE     0x77889000U
#define TEST_PARENT_FCSR_PATTERN_BASE   0x20U
#define TEST_MD_SIZE                    64
#define TEST_SIGNATURE_SIZE             64
#define TEST_PUBLIC_KEY_SIZE            32
#define TEST_SEALING_KEY_SIZE           128
#define TEST_SEALING_KDF_VERSION        1
#define TEST_LEGACY_GUARD               0x6c65676163793121ULL

static const u8 test_sm_public_key[TEST_PUBLIC_KEY_SIZE] = {
	0x4f, 0x4b, 0x65, 0x80, 0x1f, 0xab, 0x6b, 0xed,
	0xc9, 0x38, 0x11, 0x3c, 0x20, 0x7d, 0x16, 0xe2,
	0xfc, 0x2d, 0xad, 0xd8, 0xbf, 0x21, 0x9b, 0x79,
	0xc8, 0x69, 0xe1, 0x18, 0x47, 0x36, 0x04, 0xd2,
};

static const u8 test_device_public_key[TEST_PUBLIC_KEY_SIZE] = {
	0x0f, 0xaa, 0xd4, 0xff, 0x01, 0x17, 0x85, 0x83,
	0xba, 0xa5, 0x88, 0x96, 0x6f, 0x7c, 0x1f, 0xf3,
	0x25, 0x64, 0xdd, 0x17, 0xd7, 0xdc, 0x2b, 0x46,
	0xcb, 0x50, 0xa8, 0x4a, 0x69, 0x27, 0x0b, 0x4c,
};

static const u8 ed25519_group_order[32] = {
	0xed, 0xd3, 0xf5, 0x5c, 0x1a, 0x63, 0x12, 0x58,
	0xd6, 0x9c, 0xf7, 0xa2, 0xde, 0xf9, 0xde, 0x14,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10,
};

#define RISCV_REG_T0                    5
#define RISCV_REG_T1                    6
#define RISCV_REG_S0                    8
#define RISCV_REG_S1                    9
#define RISCV_REG_S2                    18
#define RISCV_REG_A0                    10
#define RISCV_REG_A1                    11
#define RISCV_REG_A2                    12
#define RISCV_REG_A6                    16
#define RISCV_REG_A7                    17
#define RISCV_FP_FS0                    8

#define RISCV_LUI(rd, imm20) \
	((((imm20) & 0xfffff) << 12) | ((rd) << 7) | 0x37)
#define RISCV_ADDI(rd, rs1, imm12) \
	((((imm12) & 0xfff) << 20) | ((rs1) << 15) | ((rd) << 7) | 0x13)
#define RISCV_BNE(rs1, rs2, imm13) \
	(((((u32)(imm13) >> 12) & 0x1) << 31) | \
	 ((((u32)(imm13) >> 5) & 0x3f) << 25) | ((rs2) << 20) | \
	 ((rs1) << 15) | (0x1 << 12) | \
	 ((((u32)(imm13) >> 1) & 0xf) << 8) | \
	 ((((u32)(imm13) >> 11) & 0x1) << 7) | 0x63)
#define RISCV_SD(rs2, rs1, imm12) \
	(((((u32)(imm12) >> 5) & 0x7f) << 25) | ((rs2) << 20) | \
	 ((rs1) << 15) | (0x3 << 12) | \
	 (((u32)(imm12) & 0x1f) << 7) | 0x23)
#define RISCV_ADD(rd, rs1, rs2) \
	(((rs2) << 20) | ((rs1) << 15) | ((rd) << 7) | 0x33)
#define RISCV_FMV_X_D(rd, fs1) \
	(0xe2000053 | ((fs1) << 15) | ((rd) << 7))
#define RISCV_FMV_D_X(fd, rs1) \
	(0xf2000053 | ((rs1) << 15) | ((fd) << 7))
#define RISCV_FRCSR(rd) \
	(0x00302073 | ((rd) << 7))
#define RISCV_FSCSR(rs1) \
	(0x00301073 | ((rs1) << 15))
#define RISCV_ECALL                     0x00000073
#define RISCV_JUMP_SELF                 0x0000006f

struct test_enclave_report {
	u8 hash[TEST_MD_SIZE];
	u64 data_len;
	u8 data[TEST_ATTEST_MAXLEN];
	u8 signature[TEST_SIGNATURE_SIZE];
};

struct test_sm_report {
	u8 hash[TEST_MD_SIZE];
	u8 public_key[TEST_PUBLIC_KEY_SIZE];
	u8 signature[TEST_SIGNATURE_SIZE];
};

struct test_report {
	struct test_enclave_report enclave;
	struct test_sm_report sm;
	u8 dev_public_key[TEST_PUBLIC_KEY_SIZE];
};

struct test_sealing_key {
	u8 key[TEST_SEALING_KEY_SIZE];
	u32 kdf_version;
	u32 reserved;
	u8 signature[TEST_SIGNATURE_SIZE];
};
#define TEST_SEALING_SIGNED_SIZE \
	(sizeof(struct test_sealing_key) - TEST_SIGNATURE_SIZE)

struct test_legacy_sealing_key {
	u8 key[TEST_SEALING_KEY_SIZE];
	u8 signature[TEST_SIGNATURE_SIZE];
};

struct zion_sbi_pregion {
	unsigned long paddr;
	size_t size;
};

struct zion_sbi_create {
	struct zion_sbi_pregion epm_region;
	struct zion_sbi_pregion utm_region;
	unsigned long runtime_paddr;
	unsigned long user_paddr;
	unsigned long free_paddr;
	unsigned long free_requested;
};

struct test_enclave {
	struct page *epm;
	struct page *utm;
	unsigned long handle;
	u64 expected_fp;
	u64 expected_fcsr;
	u64 expected_parent_fp;
	u64 expected_parent_fcsr;
	u8 expected_measurement[TEST_MD_SIZE];
};

struct destroy_racer {
	unsigned long handle;
	struct completion ready;
	struct completion *go;
	struct completion done;
	long error;
};

struct random_racer {
	struct completion ready;
	struct completion *go;
	struct completion done;
	long error;
	unsigned long value;
};

struct enclave_run_racer {
	struct test_enclave *test;
	struct completion ready;
	struct completion *go;
	struct completion done;
	int error;
};

struct enclave_migration_transition {
	struct test_enclave *test;
	unsigned long fid;
	unsigned int expected_cpu;
	struct sbiret ret;
	u64 expected_parent_fp;
	u64 expected_parent_fcsr;
	u64 actual_parent_fp;
	u64 actual_parent_fcsr;
	int error;
};

static struct test_enclave test_enclaves[2];
static unsigned long attest_challenge;
static unsigned long fresh_attest_challenge;

module_param_named(challenge, attest_challenge, ulong, 0400);
MODULE_PARM_DESC(challenge, "verifier-supplied initial attestation challenge");
module_param_named(fresh_challenge, fresh_attest_challenge, ulong, 0400);
MODULE_PARM_DESC(fresh_challenge,
		 "verifier-supplied fresh challenge used for replay rejection");

static size_t install_exit_program(void *epm_va, u32 fp_pattern,
				   u32 fcsr_pattern)
{
	u32 *program = epm_va;
	size_t pc = 0;

	BUILD_BUG_ON(TEST_REPORT_OFFSET + sizeof(struct test_report) >
		     TEST_FRESH_REPORT_OFFSET);
	BUILD_BUG_ON(TEST_FRESH_REPORT_OFFSET + sizeof(struct test_report) >
		     TEST_SEAL1_OFFSET);
	BUILD_BUG_ON(TEST_SEAL1_OFFSET + sizeof(struct test_sealing_key) >
		     TEST_SEAL2_OFFSET);
	BUILD_BUG_ON(TEST_SEAL2_OFFSET + sizeof(struct test_sealing_key) >
		     TEST_IDENT_OFFSET);
	BUILD_BUG_ON(TEST_SEAL_ERROR_OFFSET + sizeof(unsigned long) >
		     TEST_LEGACY_SEAL_OFFSET);
	BUILD_BUG_ON(TEST_LEGACY_SEAL_OFFSET +
		     sizeof(struct test_legacy_sealing_key) >
		     TEST_LEGACY_GUARD_OFFSET);
	BUILD_BUG_ON(TEST_LEGACY_GUARD_OFFSET + sizeof(u64) >
		     TEST_RANDOM1_OFFSET);
	BUILD_BUG_ON(TEST_RANDOM1_OFFSET + sizeof(u64) > TEST_RANDOM2_OFFSET);
	BUILD_BUG_ON(TEST_RANDOM2_OFFSET + sizeof(u64) >
		     TEST_HOST_FID_ERROR_OFFSET);
	BUILD_BUG_ON(TEST_HOST_FID_ERROR_OFFSET + sizeof(u64) >
		     TEST_FP_INITIAL_OFFSET);
	BUILD_BUG_ON(TEST_FP_INITIAL_OFFSET + sizeof(u64) >
		     TEST_FP_AFTER_TIMER_OFFSET);
	BUILD_BUG_ON(TEST_FP_AFTER_TIMER_OFFSET + sizeof(u64) >
		     TEST_FP_AFTER_SERVICES_OFFSET);
	BUILD_BUG_ON(TEST_FP_AFTER_SERVICES_OFFSET + sizeof(u64) >
		     TEST_FCSR_INITIAL_OFFSET);
	BUILD_BUG_ON(TEST_FCSR_INITIAL_OFFSET + sizeof(u64) >
		     TEST_FCSR_AFTER_TIMER_OFFSET);
	BUILD_BUG_ON(TEST_FCSR_AFTER_TIMER_OFFSET + sizeof(u64) >
		     TEST_FCSR_AFTER_SERVICES_OFFSET);
	BUILD_BUG_ON(TEST_FCSR_AFTER_SERVICES_OFFSET + sizeof(u64) > PAGE_SIZE);
	BUILD_BUG_ON(TEST_SEAL3_OFFSET + sizeof(struct test_sealing_key) >
		     TEST_IDENT_OFFSET);
	BUILD_BUG_ON(TEST_IDENT_OFFSET + TEST_ATTEST_DATA_SIZE >
		     TEST_FRESH_IDENT_OFFSET);
	BUILD_BUG_ON(TEST_FRESH_IDENT_OFFSET + TEST_ATTEST_DATA_SIZE >
		     TEST_ATTEST_ERROR_OFFSET);
	BUILD_BUG_ON(TEST_SEALING_SIGNED_SIZE !=
		     offsetof(struct test_sealing_key, signature));

#define EMIT(instruction) do { program[pc++] = (instruction); } while (0)
	/* Preserve the UTM base passed in a6. S2 points one page beyond it so
	 * upper-half result offsets remain representable by signed immediates. */
	EMIT(RISCV_ADDI(RISCV_REG_S0, RISCV_REG_A6, 0));
	EMIT(RISCV_LUI(RISCV_REG_S2, 1));
	EMIT(RISCV_ADD(RISCV_REG_S2, RISCV_REG_S2, RISCV_REG_S0));
	EMIT(RISCV_LUI(RISCV_REG_S1, 0x8425));
	EMIT(RISCV_ADDI(RISCV_REG_S1, RISCV_REG_S1,
			 0x08424b45 - 0x08425000));

	/* A new tee_thread must start with zero FP state. Install a per-image guard
	 * before the long loop so a machine-timer stop/resume has to preserve it. */
	EMIT(RISCV_FMV_X_D(RISCV_REG_T1, RISCV_FP_FS0));
	EMIT(RISCV_SD(RISCV_REG_T1, RISCV_REG_S2,
		       TEST_FP_INITIAL_OFFSET - PAGE_SIZE));
	EMIT(RISCV_FRCSR(RISCV_REG_T1));
	EMIT(RISCV_SD(RISCV_REG_T1, RISCV_REG_S2,
		       TEST_FCSR_INITIAL_OFFSET - PAGE_SIZE));
	EMIT(RISCV_LUI(RISCV_REG_T1, fp_pattern >> 12));
	EMIT(RISCV_ADDI(RISCV_REG_T1, RISCV_REG_T1, fp_pattern & 0xfff));
	EMIT(RISCV_FMV_D_X(RISCV_FP_FS0, RISCV_REG_T1));
	EMIT(RISCV_ADDI(RISCV_REG_T1, 0, fcsr_pattern));
	EMIT(RISCV_FSCSR(RISCV_REG_T1));

	/* Run long enough to cross a timer tick, then return TEST_EXIT_VALUE
	 * through SBI_SM_EXIT_ENCLAVE.  If the timer preempts this loop, the
	 * CVM-side test resumes the saved PC/GPR state until it reaches exit. */
	EMIT(RISCV_LUI(RISCV_REG_T0, TEST_BUSY_LOOPS >> 12));
	EMIT(RISCV_ADDI(RISCV_REG_T0, RISCV_REG_T0,
			 TEST_BUSY_LOOPS & 0xfff));
	EMIT(RISCV_ADDI(RISCV_REG_T0, RISCV_REG_T0, -1));
	EMIT(RISCV_BNE(RISCV_REG_T0, 0, -4));
	EMIT(RISCV_FMV_X_D(RISCV_REG_T1, RISCV_FP_FS0));
	EMIT(RISCV_SD(RISCV_REG_T1, RISCV_REG_S2,
		       TEST_FP_AFTER_TIMER_OFFSET - PAGE_SIZE));
	EMIT(RISCV_FRCSR(RISCV_REG_T1));
	EMIT(RISCV_SD(RISCV_REG_T1, RISCV_REG_S2,
		       TEST_FCSR_AFTER_TIMER_OFFSET - PAGE_SIZE));

	/* Oversized attestation and sealing identifiers must be rejected. */
	EMIT(RISCV_ADDI(RISCV_REG_A0, RISCV_REG_S0, TEST_REPORT_OFFSET));
	EMIT(RISCV_ADDI(RISCV_REG_A1, RISCV_REG_S2,
			 TEST_IDENT_OFFSET - PAGE_SIZE));
	EMIT(RISCV_ADDI(RISCV_REG_A2, 0, TEST_ATTEST_MAXLEN + 1));
	EMIT(RISCV_LUI(RISCV_REG_A6, 1));
	EMIT(RISCV_ADDI(RISCV_REG_A6, RISCV_REG_A6, 3002 - 4096));
	EMIT(RISCV_ADDI(RISCV_REG_A7, RISCV_REG_S1, 0));
	EMIT(RISCV_ECALL);
	EMIT(RISCV_SD(RISCV_REG_A0, RISCV_REG_S2,
		       TEST_ATTEST_ERROR_OFFSET - PAGE_SIZE));

	EMIT(RISCV_ADDI(RISCV_REG_A0, RISCV_REG_S2,
			 TEST_SEAL1_OFFSET - PAGE_SIZE));
	EMIT(RISCV_ADDI(RISCV_REG_A1, RISCV_REG_S2,
			 TEST_IDENT_OFFSET - PAGE_SIZE));
	EMIT(RISCV_ADDI(RISCV_REG_A2, 0, TEST_ATTEST_MAXLEN + 1));
	EMIT(RISCV_LUI(RISCV_REG_A6, 1));
	EMIT(RISCV_ADDI(RISCV_REG_A6, RISCV_REG_A6, 3007 - 4096));
	EMIT(RISCV_ADDI(RISCV_REG_A7, RISCV_REG_S1, 0));
	EMIT(RISCV_ECALL);
	EMIT(RISCV_SD(RISCV_REG_A0, RISCV_REG_S2,
		       TEST_SEAL_ERROR_OFFSET - PAGE_SIZE));

	/* Produce a report for the first challenge and derive the same sealing
	 * key twice from that identity. */
	EMIT(RISCV_ADDI(RISCV_REG_A0, RISCV_REG_S0, TEST_REPORT_OFFSET));
	EMIT(RISCV_ADDI(RISCV_REG_A1, RISCV_REG_S2,
			 TEST_IDENT_OFFSET - PAGE_SIZE));
	EMIT(RISCV_ADDI(RISCV_REG_A2, 0, TEST_ATTEST_DATA_SIZE));
	EMIT(RISCV_LUI(RISCV_REG_A6, 1));
	EMIT(RISCV_ADDI(RISCV_REG_A6, RISCV_REG_A6, 3002 - 4096));
	EMIT(RISCV_ADDI(RISCV_REG_A7, RISCV_REG_S1, 0));
	EMIT(RISCV_ECALL);

	EMIT(RISCV_ADDI(RISCV_REG_A0, RISCV_REG_S2,
			 TEST_SEAL1_OFFSET - PAGE_SIZE));
	EMIT(RISCV_ADDI(RISCV_REG_A1, RISCV_REG_S2,
			 TEST_IDENT_OFFSET - PAGE_SIZE));
	EMIT(RISCV_ADDI(RISCV_REG_A2, 0, TEST_ATTEST_DATA_SIZE));
	EMIT(RISCV_LUI(RISCV_REG_A6, 1));
	EMIT(RISCV_ADDI(RISCV_REG_A6, RISCV_REG_A6, 3007 - 4096));
	EMIT(RISCV_ADDI(RISCV_REG_A7, RISCV_REG_S1, 0));
	EMIT(RISCV_ECALL);

	EMIT(RISCV_ADDI(RISCV_REG_A0, RISCV_REG_S2,
			 TEST_SEAL2_OFFSET - PAGE_SIZE));
	EMIT(RISCV_ADDI(RISCV_REG_A1, RISCV_REG_S2,
			 TEST_IDENT_OFFSET - PAGE_SIZE));
	EMIT(RISCV_ADDI(RISCV_REG_A2, 0, TEST_ATTEST_DATA_SIZE));
	EMIT(RISCV_LUI(RISCV_REG_A6, 1));
	EMIT(RISCV_ADDI(RISCV_REG_A6, RISCV_REG_A6, 3007 - 4096));
	EMIT(RISCV_ADDI(RISCV_REG_A7, RISCV_REG_S1, 0));
	EMIT(RISCV_ECALL);

	/* The original call must retain its 192-byte key-plus-signature ABI. */
	EMIT(RISCV_ADDI(RISCV_REG_A0, RISCV_REG_S2,
			 TEST_LEGACY_SEAL_OFFSET - PAGE_SIZE));
	EMIT(RISCV_ADDI(RISCV_REG_A1, RISCV_REG_S2,
			 TEST_IDENT_OFFSET - PAGE_SIZE));
	EMIT(RISCV_ADDI(RISCV_REG_A2, 0, TEST_ATTEST_DATA_SIZE));
	EMIT(RISCV_LUI(RISCV_REG_A6, 1));
	EMIT(RISCV_ADDI(RISCV_REG_A6, RISCV_REG_A6, 3003 - 4096));
	EMIT(RISCV_ADDI(RISCV_REG_A7, RISCV_REG_S1, 0));
	EMIT(RISCV_ECALL);

	/* Two random requests must consume distinct global counter values. */
	EMIT(RISCV_LUI(RISCV_REG_A6, 1));
	EMIT(RISCV_ADDI(RISCV_REG_A6, RISCV_REG_A6, 3001 - 4096));
	EMIT(RISCV_ADDI(RISCV_REG_A7, RISCV_REG_S1, 0));
	EMIT(RISCV_ECALL);
	EMIT(RISCV_SD(RISCV_REG_A1, RISCV_REG_S2,
		       TEST_RANDOM1_OFFSET - PAGE_SIZE));
	EMIT(RISCV_LUI(RISCV_REG_A6, 1));
	EMIT(RISCV_ADDI(RISCV_REG_A6, RISCV_REG_A6, 3001 - 4096));
	EMIT(RISCV_ADDI(RISCV_REG_A7, RISCV_REG_S1, 0));
	EMIT(RISCV_ECALL);
	EMIT(RISCV_SD(RISCV_REG_A1, RISCV_REG_S2,
		       TEST_RANDOM2_OFFSET - PAGE_SIZE));

	/* Enclave callers must not reach host-management FIDs. */
	EMIT(RISCV_ADDI(RISCV_REG_A0, 0, 0));
	EMIT(RISCV_ADDI(RISCV_REG_A6, 0, SBI_SM_CREATE_ENCLAVE));
	EMIT(RISCV_ADDI(RISCV_REG_A7, RISCV_REG_S1, 0));
	EMIT(RISCV_ECALL);
	EMIT(RISCV_SD(RISCV_REG_A0, RISCV_REG_S2,
		       TEST_HOST_FID_ERROR_OFFSET - PAGE_SIZE));

	/* A different identity must select a different sealing-key domain. */
	EMIT(RISCV_ADDI(RISCV_REG_A0, RISCV_REG_S2,
			 TEST_SEAL3_OFFSET - PAGE_SIZE));
	EMIT(RISCV_ADDI(RISCV_REG_A1, RISCV_REG_S2,
			 TEST_FRESH_IDENT_OFFSET - PAGE_SIZE));
	EMIT(RISCV_ADDI(RISCV_REG_A2, 0, TEST_ATTEST_DATA_SIZE));
	EMIT(RISCV_LUI(RISCV_REG_A6, 1));
	EMIT(RISCV_ADDI(RISCV_REG_A6, RISCV_REG_A6, 3007 - 4096));
	EMIT(RISCV_ADDI(RISCV_REG_A7, RISCV_REG_S1, 0));
	EMIT(RISCV_ECALL);

	/* A second valid report binds a fresh verifier challenge. */
	EMIT(RISCV_ADDI(RISCV_REG_A0, RISCV_REG_S0,
			 TEST_FRESH_REPORT_OFFSET));
	EMIT(RISCV_ADDI(RISCV_REG_A1, RISCV_REG_S2,
			 TEST_FRESH_IDENT_OFFSET - PAGE_SIZE));
	EMIT(RISCV_ADDI(RISCV_REG_A2, 0, TEST_ATTEST_DATA_SIZE));
	EMIT(RISCV_LUI(RISCV_REG_A6, 1));
	EMIT(RISCV_ADDI(RISCV_REG_A6, RISCV_REG_A6, 3002 - 4096));
	EMIT(RISCV_ADDI(RISCV_REG_A7, RISCV_REG_S1, 0));
	EMIT(RISCV_ECALL);

	/* The checked monitor services above must not perturb enclave FP state. */
	EMIT(RISCV_FMV_X_D(RISCV_REG_T1, RISCV_FP_FS0));
	EMIT(RISCV_SD(RISCV_REG_T1, RISCV_REG_S2,
		       TEST_FP_AFTER_SERVICES_OFFSET - PAGE_SIZE));
	EMIT(RISCV_FRCSR(RISCV_REG_T1));
	EMIT(RISCV_SD(RISCV_REG_T1, RISCV_REG_S2,
		       TEST_FCSR_AFTER_SERVICES_OFFSET - PAGE_SIZE));

	EMIT(RISCV_ADDI(RISCV_REG_A0, 0, TEST_EXIT_VALUE));
	EMIT(RISCV_LUI(RISCV_REG_A6, 1));
	EMIT(RISCV_ADDI(RISCV_REG_A6, RISCV_REG_A6, 3006 - 4096));
	EMIT(RISCV_ADDI(RISCV_REG_A7, RISCV_REG_S1, 0));
	EMIT(RISCV_ECALL);
	EMIT(RISCV_JUMP_SELF);
	asm volatile("fence.i" ::: "memory");
#undef EMIT
	return pc;
}

static bool buffer_is_zero(const u8 *buffer, size_t size)
{
	for (size_t i = 0; i < size; i++) {
		if (buffer[i])
			return false;
	}
	return true;
}

static void malleate_signature_scalar(u8 signature[TEST_SIGNATURE_SIZE])
{
	unsigned int carry = 0;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(ed25519_group_order); i++) {
		unsigned int sum = signature[32 + i] +
				   ed25519_group_order[i] + carry;

		signature[32 + i] = sum;
		carry = sum >> 8;
	}
}

static int measure_test_epm(struct test_enclave *test, const void *epm_va)
{
	struct crypto_shash *tfm;
	struct shash_desc *desc;
	unsigned long sizes[3] = { 0, PAGE_SIZE, PAGE_SIZE };
	int ret;

	tfm = crypto_alloc_shash("sha3-512", 0, 0);
	if (IS_ERR(tfm))
		return PTR_ERR(tfm);
	desc = kmalloc(sizeof(*desc) + crypto_shash_descsize(tfm), GFP_KERNEL);
	if (!desc) {
		crypto_free_shash(tfm);
		return -ENOMEM;
	}
	desc->tfm = tfm;

	ret = crypto_shash_init(desc);
	if (!ret)
		ret = crypto_shash_update(desc, (const u8 *)sizes,
					  sizeof(sizes));
	if (!ret)
		ret = crypto_shash_update(desc, epm_va, 2 * PAGE_SIZE);
	if (!ret)
		ret = crypto_shash_final(desc, test->expected_measurement);

	kfree_sensitive(desc);
	crypto_free_shash(tfm);
	return ret;
}

static int validate_attestation_outputs(struct test_enclave *test)
{
	u8 *utm = page_address(test->utm);
	struct test_report *report =
		(struct test_report *)(utm + TEST_REPORT_OFFSET);
	struct test_report *fresh_report =
		(struct test_report *)(utm + TEST_FRESH_REPORT_OFFSET);
	struct test_sealing_key *seal1 =
		(struct test_sealing_key *)(utm + TEST_SEAL1_OFFSET);
	struct test_sealing_key *seal2 =
		(struct test_sealing_key *)(utm + TEST_SEAL2_OFFSET);
	struct test_sealing_key *seal3 =
		(struct test_sealing_key *)(utm + TEST_SEAL3_OFFSET);
	struct test_legacy_sealing_key *legacy_seal =
		(struct test_legacy_sealing_key *)(utm + TEST_LEGACY_SEAL_OFFSET);
	u64 legacy_guard = *(u64 *)(utm + TEST_LEGACY_GUARD_OFFSET);
	u64 random1 = *(u64 *)(utm + TEST_RANDOM1_OFFSET);
	u64 random2 = *(u64 *)(utm + TEST_RANDOM2_OFFSET);
	u64 host_fid_error = *(u64 *)(utm + TEST_HOST_FID_ERROR_OFFSET);
	u64 fp_initial = *(u64 *)(utm + TEST_FP_INITIAL_OFFSET);
	u64 fp_after_timer = *(u64 *)(utm + TEST_FP_AFTER_TIMER_OFFSET);
	u64 fp_after_services = *(u64 *)(utm + TEST_FP_AFTER_SERVICES_OFFSET);
	u64 fcsr_initial = *(u64 *)(utm + TEST_FCSR_INITIAL_OFFSET);
	u64 fcsr_after_timer = *(u64 *)(utm + TEST_FCSR_AFTER_TIMER_OFFSET);
	u64 fcsr_after_services =
		*(u64 *)(utm + TEST_FCSR_AFTER_SERVICES_OFFSET);
	unsigned long attest_error =
		*(unsigned long *)(utm + TEST_ATTEST_ERROR_OFFSET);
	unsigned long seal_error =
		*(unsigned long *)(utm + TEST_SEAL_ERROR_OFFSET);
	unsigned long report_data;
	unsigned long fresh_report_data;
	u8 tampered_signature[TEST_SIGNATURE_SIZE];
	u8 forged_public_key[TEST_PUBLIC_KEY_SIZE];
	size_t enclave_signed_size;
	size_t fresh_enclave_signed_size;
	bool tamper_accepted;

	if (attest_error != SM_ENCLAVE_ILLEGAL_ARGUMENT ||
	    seal_error != SM_ENCLAVE_ILLEGAL_ARGUMENT ||
	    host_fid_error != SM_ENCLAVE_SBI_PROHIBITED) {
		pr_err("[CVM ENCLAVE] rejected calls returned %lu, %lu, %llu\n",
		       attest_error, seal_error, host_fid_error);
		return -EINVAL;
	}
	if (fp_initial || fcsr_initial ||
	    fp_after_timer != test->expected_fp ||
	    fp_after_services != test->expected_fp ||
	    fcsr_after_timer != test->expected_fcsr ||
	    fcsr_after_services != test->expected_fcsr) {
		pr_err("[CVM ENCLAVE] FP state initial=%llx/%llx timer=%llx/%llx services=%llx/%llx expected=%llx/%x\n",
		       fp_initial, fcsr_initial, fp_after_timer,
		       fcsr_after_timer, fp_after_services,
		       fcsr_after_services, test->expected_fp,
		       (u32)test->expected_fcsr);
		return -EINVAL;
	}
	if (report->enclave.data_len != TEST_ATTEST_DATA_SIZE ||
	    fresh_report->enclave.data_len != TEST_ATTEST_DATA_SIZE) {
		pr_err("[CVM ENCLAVE] report data lengths=%llu, %llu\n",
		       report->enclave.data_len,
		       fresh_report->enclave.data_len);
		return -EINVAL;
	}
	enclave_signed_size = TEST_MD_SIZE + sizeof(report->enclave.data_len) +
			      report->enclave.data_len;
	fresh_enclave_signed_size = TEST_MD_SIZE +
				    sizeof(fresh_report->enclave.data_len) +
				    fresh_report->enclave.data_len;
	memcpy(&report_data, report->enclave.data, sizeof(report_data));
	memcpy(&fresh_report_data, fresh_report->enclave.data,
	       sizeof(fresh_report_data));
	if (report_data != attest_challenge ||
	    fresh_report_data != fresh_attest_challenge ||
	    report_data == fresh_attest_challenge ||
	    fresh_report_data == attest_challenge ||
	    memcmp(report->enclave.hash, test->expected_measurement,
		   sizeof(test->expected_measurement)) ||
	    memcmp(fresh_report->enclave.hash, test->expected_measurement,
		   sizeof(test->expected_measurement)) ||
	    buffer_is_zero(report->enclave.signature,
			   sizeof(report->enclave.signature)) ||
	    buffer_is_zero(fresh_report->enclave.signature,
			   sizeof(fresh_report->enclave.signature)) ||
	    buffer_is_zero(report->sm.hash, sizeof(report->sm.hash)) ||
	    memcmp(report->sm.public_key, test_sm_public_key,
		   sizeof(test_sm_public_key)) ||
	    buffer_is_zero(report->sm.signature,
			   sizeof(report->sm.signature)) ||
	    memcmp(report->dev_public_key, test_device_public_key,
		   sizeof(test_device_public_key)) ||
	    memcmp(&fresh_report->sm, &report->sm, sizeof(report->sm)) ||
	    memcmp(fresh_report->dev_public_key, report->dev_public_key,
		   sizeof(report->dev_public_key))) {
		pr_err("[CVM ENCLAVE] attestation report is incomplete\n");
		return -EINVAL;
	}
	if (!cvm_ed25519_verify(report->sm.signature, &report->sm,
				 TEST_MD_SIZE + TEST_PUBLIC_KEY_SIZE,
				 report->dev_public_key) ||
	    !cvm_ed25519_verify(report->enclave.signature, &report->enclave,
				 enclave_signed_size, report->sm.public_key) ||
	    !cvm_ed25519_verify(fresh_report->enclave.signature,
				 &fresh_report->enclave,
				 fresh_enclave_signed_size,
				 fresh_report->sm.public_key) ||
	    !cvm_ed25519_verify(seal1->signature, seal1,
				 TEST_SEALING_SIGNED_SIZE, report->sm.public_key) ||
	    !cvm_ed25519_verify(seal2->signature, seal2,
				 TEST_SEALING_SIGNED_SIZE, report->sm.public_key) ||
	    !cvm_ed25519_verify(seal3->signature, seal3,
				 TEST_SEALING_SIGNED_SIZE, report->sm.public_key) ||
	    !cvm_ed25519_verify(legacy_seal->signature, legacy_seal->key,
				 sizeof(legacy_seal->key), report->sm.public_key)) {
		pr_err("[CVM ENCLAVE] attestation signature chain is invalid\n");
		return -EKEYREJECTED;
	}

	memcpy(tampered_signature, report->sm.signature,
	       sizeof(tampered_signature));
	tampered_signature[0] ^= 1;
	if (cvm_ed25519_verify(tampered_signature, &report->sm,
			       TEST_MD_SIZE + TEST_PUBLIC_KEY_SIZE,
			       report->dev_public_key)) {
		pr_err("[CVM ENCLAVE] mutated SM certificate was accepted\n");
		return -EKEYREJECTED;
	}
	memcpy(tampered_signature, report->enclave.signature,
	       sizeof(tampered_signature));
	tampered_signature[0] ^= 1;
	if (cvm_ed25519_verify(tampered_signature, &report->enclave,
			       enclave_signed_size, report->sm.public_key)) {
		pr_err("[CVM ENCLAVE] mutated enclave signature was accepted\n");
		return -EKEYREJECTED;
	}

	report->sm.hash[0] ^= 1;
	tamper_accepted = cvm_ed25519_verify(report->sm.signature, &report->sm,
					     TEST_MD_SIZE + TEST_PUBLIC_KEY_SIZE,
					     report->dev_public_key);
	report->sm.hash[0] ^= 1;
	if (tamper_accepted) {
		pr_err("[CVM ENCLAVE] mutated SM measurement was accepted\n");
		return -EKEYREJECTED;
	}
	report->enclave.data[0] ^= 1;
	tamper_accepted = cvm_ed25519_verify(report->enclave.signature,
					     &report->enclave,
					     enclave_signed_size,
					     report->sm.public_key);
	report->enclave.data[0] ^= 1;
	if (tamper_accepted) {
		pr_err("[CVM ENCLAVE] mutated report data was accepted\n");
		return -EKEYREJECTED;
	}
	if (cvm_ed25519_verify(report->enclave.signature, &report->enclave,
			       enclave_signed_size, report->dev_public_key)) {
		pr_err("[CVM ENCLAVE] enclave signature accepted under device key\n");
		return -EKEYREJECTED;
	}
	memcpy(tampered_signature, report->enclave.signature,
	       sizeof(tampered_signature));
	malleate_signature_scalar(tampered_signature);
	if (cvm_ed25519_verify(tampered_signature, &report->enclave,
			       enclave_signed_size, report->sm.public_key)) {
		pr_err("[CVM ENCLAVE] non-canonical signature scalar was accepted\n");
		return -EKEYREJECTED;
	}
	memset(forged_public_key, 0, sizeof(forged_public_key));
	forged_public_key[0] = 1;
	memset(tampered_signature, 0, sizeof(tampered_signature));
	tampered_signature[0] = 1;
	if (cvm_ed25519_verify(tampered_signature, &report->enclave,
			       enclave_signed_size, forged_public_key)) {
		pr_err("[CVM ENCLAVE] small-order identity forgery was accepted\n");
		return -EKEYREJECTED;
	}
	seal1->kdf_version ^= 1;
	tamper_accepted = cvm_ed25519_verify(seal1->signature, seal1,
					     TEST_SEALING_SIGNED_SIZE,
					     report->sm.public_key);
	seal1->kdf_version ^= 1;
	if (tamper_accepted) {
		pr_err("[CVM ENCLAVE] mutated sealing KDF version was accepted\n");
		return -EKEYREJECTED;
	}
	if (random1 == random2) {
		pr_err("[CVM ENCLAVE] consecutive random calls repeated a value\n");
		return -EKEYREJECTED;
	}
	if (buffer_is_zero(seal1->key, sizeof(seal1->key)) ||
	    buffer_is_zero(seal1->signature, sizeof(seal1->signature)) ||
	    buffer_is_zero(seal3->key, sizeof(seal3->key)) ||
	    seal1->kdf_version != TEST_SEALING_KDF_VERSION ||
	    seal2->kdf_version != TEST_SEALING_KDF_VERSION ||
	    seal3->kdf_version != TEST_SEALING_KDF_VERSION ||
	    seal1->reserved || seal2->reserved || seal3->reserved ||
	    memcmp(seal1, seal2, sizeof(*seal1)) ||
	    memcmp(seal1->key, legacy_seal->key, sizeof(seal1->key)) ||
	    legacy_guard != TEST_LEGACY_GUARD ||
	    !memcmp(seal1->key, seal3->key, sizeof(seal1->key))) {
		pr_err("[CVM ENCLAVE] sealing key output is invalid or unstable\n");
		return -EINVAL;
	}

	memzero_explicit(tampered_signature, sizeof(tampered_signature));
	memzero_explicit(forged_public_key, sizeof(forged_public_key));
	pr_info("[CVM ENCLAVE] PASS: independent attestation, freshness/replay, tamper rejection, signed KDF v1, legacy 192-byte sealing ABI, and random uniqueness validation completed (%016lx -> %016lx)\n",
		attest_challenge, fresh_attest_challenge);
	return 0;
}

static struct sbiret enclave_ecall(unsigned long fid, unsigned long arg0)
{
	return sbi_ecall(ZION_SBI_EXT, fid, arg0, 0, 0, 0, 0, 0);
}

static void install_parent_fp_guard(u64 expected_fp, u64 expected_fcsr)
{
	unsigned long fp = expected_fp;
	unsigned long fcsr = expected_fcsr;

	asm volatile(".option push\n\t"
		     ".option arch, +f\n\t"
		     ".option arch, +d\n\t"
		     "fmv.d.x fs0, %0\n\t"
		     "fscsr %1\n\t"
		     ".option pop"
		     : : "r" (fp), "r" (fcsr) : "memory");
}

static bool parent_fp_guard_matches(u64 expected_fp, u64 expected_fcsr,
				    u64 *actual_fp, u64 *actual_fcsr)
{
	unsigned long fp;
	unsigned long fcsr;

	asm volatile(".option push\n\t"
		     ".option arch, +f\n\t"
		     ".option arch, +d\n\t"
		     "fmv.x.d %0, fs0\n\t"
		     "frcsr %1\n\t"
		     ".option pop"
		     : "=r" (fp), "=r" (fcsr) : : "memory");
	*actual_fp = fp;
	*actual_fcsr = fcsr;
	return fp == expected_fp && fcsr == expected_fcsr;
}

static int random_racer_fn(void *data)
{
	struct random_racer *racer = data;
	struct sbiret ret;

	complete(&racer->ready);
	wait_for_completion(racer->go);
	ret = enclave_ecall(SBI_SM_RANDOM, 0);
	racer->error = ret.error;
	racer->value = ret.value;
	complete(&racer->done);
	return 0;
}

static int test_random_access_control(void)
{
	struct completion go;
	struct random_racer racers[2];
	struct task_struct *tasks[2];
	struct sbiret plugin_ret;

	if (!cpu_online(0) || !cpu_online(1))
		return -EINVAL;
	init_completion(&go);
	for (int i = 0; i < ARRAY_SIZE(racers); i++) {
		racers[i].go = &go;
		racers[i].error = -1;
		racers[i].value = 0;
		init_completion(&racers[i].ready);
		init_completion(&racers[i].done);
		tasks[i] = kthread_create(random_racer_fn, &racers[i],
					  "cvm-encl-random/%d", i);
		if (IS_ERR(tasks[i])) {
			if (i == 1) {
				complete_all(&go);
				wait_for_completion(&racers[0].done);
			}
			return -ENOMEM;
		}
		kthread_bind(tasks[i], i);
		wake_up_process(tasks[i]);
	}
	wait_for_completion(&racers[0].ready);
	wait_for_completion(&racers[1].ready);
	complete_all(&go);
	wait_for_completion(&racers[0].done);
	wait_for_completion(&racers[1].done);

	plugin_ret = enclave_ecall(SBI_SM_CALL_PLUGIN, 0);
	if (racers[0].error != SM_ENCLAVE_SBI_PROHIBITED ||
	    racers[1].error != SM_ENCLAVE_SBI_PROHIBITED ||
	    plugin_ret.error != SM_ENCLAVE_SBI_PROHIBITED) {
		pr_err("[CVM ENCLAVE] non-enclave calls returned random=(%ld, %lx), (%ld, %lx) plugin=%ld\n",
		       racers[0].error, racers[0].value,
		       racers[1].error, racers[1].value, plugin_ret.error);
		return -EKEYREJECTED;
	}
	pr_info("[CVM ENCLAVE] PASS: cross-CPU non-enclave random and plugin calls rejected\n");
	return 0;
}

static int test_invalid_create_args(void)
{
	struct zion_sbi_create args;
	struct page *epm;
	struct page *utm;
	struct sbiret ret;
	unsigned long epm_pa;

	epm = alloc_pages(GFP_KERNEL | __GFP_ZERO, TEST_EPM_ORDER);
	utm = alloc_page(GFP_KERNEL | __GFP_ZERO);
	if (!epm || !utm) {
		if (utm)
			__free_page(utm);
		if (epm)
			__free_pages(epm, TEST_EPM_ORDER);
		return -ENOMEM;
	}

	epm_pa = page_to_phys(epm);
	memset(&args, 0, sizeof(args));
	args.epm_region.paddr = epm_pa;
	args.epm_region.size = TEST_EPM_SIZE;
	args.utm_region.paddr = page_to_phys(utm);
	args.utm_region.size = PAGE_SIZE;
	args.runtime_paddr = epm_pa;
	args.user_paddr = epm_pa + PAGE_SIZE;
	args.free_paddr = epm_pa + 2 * PAGE_SIZE;
	args.free_requested = TEST_EPM_SIZE;

	ret = enclave_ecall(SBI_SM_CREATE_ENCLAVE, (unsigned long)&args);
	if (ret.error != SM_ENCLAVE_ILLEGAL_ARGUMENT) {
		pr_err("[CVM ENCLAVE] oversized free region returned %ld\n",
		       ret.error);
		goto failed;
	}

	args.free_requested = PAGE_SIZE;
	args.utm_region.paddr = epm_pa + PAGE_SIZE;
	ret = enclave_ecall(SBI_SM_CREATE_ENCLAVE, (unsigned long)&args);
	if (ret.error != SM_ENCLAVE_ILLEGAL_ARGUMENT) {
		pr_err("[CVM ENCLAVE] overlapping EPM/UTM returned %ld\n",
		       ret.error);
		goto failed;
	}

	__free_page(utm);
	__free_pages(epm, TEST_EPM_ORDER);
	pr_info("[CVM ENCLAVE] PASS: invalid create ranges rejected\n");
	return 0;

failed:
	__free_page(utm);
	__free_pages(epm, TEST_EPM_ORDER);
	return -EINVAL;
}

static int test_cvm_management_access_control(void)
{
	static const unsigned long management_fids[] = {
		SBI_SM_CREATE_CVM,
		SBI_SM_LOAD_MEM,
		SBI_SM_REGISTER_PT,
	};

	for (size_t i = 0; i < ARRAY_SIZE(management_fids); i++) {
		struct sbiret ret = enclave_ecall(management_fids[i], 0);

		if (ret.error != SBI_ERR_DENIED) {
			pr_err("[CVM ENCLAVE] CVM management FID %lu returned %ld\n",
			       management_fids[i], ret.error);
			return -EACCES;
		}
	}
	pr_info("[CVM ENCLAVE] PASS: parent CVM management calls denied\n");
	return 0;
}

static void release_test_memory(struct test_enclave *test)
{
	if (test->utm) {
		__free_page(test->utm);
		test->utm = NULL;
	}
	if (test->epm) {
		__free_pages(test->epm, TEST_EPM_ORDER);
		test->epm = NULL;
	}
}

static int create_test_enclave(struct test_enclave *test,
			       unsigned long measurement_variant)
{
	struct zion_sbi_create args;
	struct sbiret ret;
	unsigned long epm_pa;
	unsigned long utm_pa;
	void *epm_va;
	size_t program_words;
	int error = -ENOMEM;

	test->epm = alloc_pages(GFP_KERNEL | __GFP_ZERO, TEST_EPM_ORDER);
	test->utm = alloc_page(GFP_KERNEL | __GFP_ZERO);
	if (!test->epm || !test->utm)
		goto no_memory;

	epm_pa = page_to_phys(test->epm);
	utm_pa = page_to_phys(test->utm);
	epm_va = page_address(test->epm);

	/* Touch every page so the outer SM has materialized all parent GPA
	 * mappings before it copies the EPM into enclave-owned blocks. Use a
	 * deterministic page marker rather than the allocation's physical address
	 * so identical enclave images have stable measurements. */
	for (size_t offset = 0; offset < TEST_EPM_SIZE; offset += PAGE_SIZE)
		*(unsigned long *)(epm_va + offset) = offset / PAGE_SIZE;
	test->expected_fp = TEST_FP_PATTERN_BASE + measurement_variant;
	test->expected_fcsr = TEST_FCSR_PATTERN_BASE + measurement_variant;
	test->expected_parent_fp =
		TEST_PARENT_FP_PATTERN_BASE + measurement_variant;
	test->expected_parent_fcsr =
		TEST_PARENT_FCSR_PATTERN_BASE + measurement_variant;
	program_words = install_exit_program(epm_va, (u32)test->expected_fp,
				     (u32)test->expected_fcsr);
	if (program_words * sizeof(u32) > TEST_VARIANT_OFFSET) {
		pr_err("[CVM ENCLAVE] test payload overlaps measurement variant\n");
		error = -E2BIG;
		goto failed;
	}
	*(unsigned long *)(epm_va + TEST_VARIANT_OFFSET) = measurement_variant;
	error = measure_test_epm(test, epm_va);
	if (error) {
		pr_err("[CVM ENCLAVE] could not compute expected measurement: %d\n",
		       error);
		goto failed;
	}
	*(unsigned long *)page_address(test->utm) = 0x5a5a5a5a5a5a5a5aUL;

	memset(&args, 0, sizeof(args));
	args.epm_region.paddr = epm_pa;
	args.epm_region.size = TEST_EPM_SIZE;
	args.utm_region.paddr = utm_pa;
	args.utm_region.size = PAGE_SIZE;
	args.runtime_paddr = epm_pa;
	args.user_paddr = epm_pa + PAGE_SIZE;
	args.free_paddr = epm_pa + 2 * PAGE_SIZE;
	args.free_requested = PAGE_SIZE;

	ret = enclave_ecall(SBI_SM_CREATE_ENCLAVE, (unsigned long)&args);
	if (ret.error) {
		pr_err("[CVM ENCLAVE] create failed: %ld\n", ret.error);
		error = -EINVAL;
		goto failed;
	}

	/* Creation must scrub the parent-owned UTM through its GPA mapping. */
	if (*(unsigned long *)page_address(test->utm) != 0) {
		pr_err("[CVM ENCLAVE] UTM was not cleared\n");
		enclave_ecall(SBI_SM_DESTROY_ENCLAVE, ret.value);
		error = -EINVAL;
		goto failed;
	}

	test->handle = ret.value;
	*(unsigned long *)(page_address(test->utm) + TEST_IDENT_OFFSET) =
		attest_challenge;
	*(unsigned long *)(page_address(test->utm) +
			  TEST_FRESH_IDENT_OFFSET) = fresh_attest_challenge;
	*(u64 *)(page_address(test->utm) + TEST_LEGACY_GUARD_OFFSET) =
		TEST_LEGACY_GUARD;
	return 0;

no_memory:
	pr_err("[CVM ENCLAVE] could not allocate test EPM/UTM\n");
failed:
	release_test_memory(test);
	return error;
}

static int destroy_test_enclave(struct test_enclave *test)
{
	struct sbiret ret = enclave_ecall(SBI_SM_DESTROY_ENCLAVE, test->handle);

	if (ret.error) {
		pr_err("[CVM ENCLAVE] destroy handle=%lu failed: %ld\n",
		       test->handle, ret.error);
		return -EINVAL;
	}
	release_test_memory(test);
	return 0;
}

static int destroy_racer_fn(void *data)
{
	struct destroy_racer *racer = data;

	complete(&racer->ready);
	wait_for_completion(racer->go);
	racer->error = enclave_ecall(SBI_SM_DESTROY_ENCLAVE,
				     racer->handle).error;
	complete(&racer->done);
	return 0;
}

static int race_destroy_test_enclave(struct test_enclave *test)
{
	struct completion go;
	struct destroy_racer racers[2];
	struct task_struct *tasks[2];
	int successes = 0;
	int rejected = 0;

	if (!cpu_online(0) || !cpu_online(1)) {
		pr_err("[CVM ENCLAVE] CPU0/CPU1 are not both online\n");
		return -EINVAL;
	}

	init_completion(&go);
	for (int i = 0; i < ARRAY_SIZE(racers); i++) {
		racers[i].handle = test->handle;
		racers[i].go = &go;
		racers[i].error = -1;
		init_completion(&racers[i].ready);
		init_completion(&racers[i].done);
		tasks[i] = kthread_create(destroy_racer_fn, &racers[i],
					  "cvm-encl-destroy/%d", i);
		if (IS_ERR(tasks[i])) {
			pr_err("[CVM ENCLAVE] could not create destroy racer %d\n",
			       i);
			if (i == 1) {
				complete_all(&go);
				wait_for_completion(&racers[0].done);
			}
			return -ENOMEM;
		}
		kthread_bind(tasks[i], i);
		wake_up_process(tasks[i]);
	}

	wait_for_completion(&racers[0].ready);
	wait_for_completion(&racers[1].ready);
	complete_all(&go);
	wait_for_completion(&racers[0].done);
	wait_for_completion(&racers[1].done);

	for (int i = 0; i < ARRAY_SIZE(racers); i++) {
		if (!racers[i].error)
			successes++;
		else if (racers[i].error == SM_ENCLAVE_NOT_DESTROYABLE)
			rejected++;
	}

	if (successes == 1)
		release_test_memory(test);
	if (successes != 1 || rejected != 1) {
		pr_err("[CVM ENCLAVE] destroy race returned %ld, %ld\n",
		       racers[0].error, racers[1].error);
		return -EINVAL;
	}

	pr_info("[CVM ENCLAVE] PASS: concurrent destroy serialized\n");
	return 0;
}

static int run_test_enclave(struct test_enclave *test)
{
	struct sbiret ret;
	unsigned long irq_flags_before;
	unsigned long irq_flags_after;
	unsigned long run_fid = SBI_SM_RUN_ENCLAVE;
	u64 actual_parent_fp = 0;
	u64 actual_parent_fcsr = 0;
	unsigned int resumes = 0;
	bool irq_mismatch = false;
	bool fp_mismatch = false;

	if (!kernel_fpu_available()) {
		pr_err("[CVM ENCLAVE] parent kernel FPU is unavailable\n");
		return -EOPNOTSUPP;
	}

	/* Keep this kthread on its bound vCPU while its kernel-mode guard is live.
	 * The SM must return that exact parent state after every timer stop and the
	 * final nested exit, while Linux restores the interrupted task afterwards. */
	kernel_fpu_begin();
	install_parent_fp_guard(test->expected_parent_fp,
				 test->expected_parent_fcsr);

	do {
		local_save_flags(irq_flags_before);
		ret = enclave_ecall(run_fid, test->handle);
		if (!parent_fp_guard_matches(test->expected_parent_fp,
					     test->expected_parent_fcsr,
					     &actual_parent_fp,
					     &actual_parent_fcsr)) {
			fp_mismatch = true;
			break;
		}
		local_save_flags(irq_flags_after);
		if (irqs_disabled_flags(irq_flags_before) !=
		    irqs_disabled_flags(irq_flags_after)) {
			irq_mismatch = true;
			break;
		}
		if (ret.error != SM_ENCLAVE_INTERRUPTED)
			break;
		run_fid = SBI_SM_RESUME_ENCLAVE;
		resumes++;
	} while (resumes < TEST_MAX_RESUMES);
	kernel_fpu_end();

	if (fp_mismatch) {
		pr_err("[CVM ENCLAVE] parent FP guard changed across run/resume: %llx/%llx expected %llx/%llx\n",
		       actual_parent_fp, actual_parent_fcsr,
		       test->expected_parent_fp, test->expected_parent_fcsr);
		return -EKEYREJECTED;
	}
	if (irq_mismatch) {
		pr_err("[CVM ENCLAVE] IRQ state changed across run/resume: before=0x%lx after=0x%lx\n",
		       irq_flags_before, irq_flags_after);
		return -EINVAL;
	}

	if (ret.error || ret.value != TEST_EXIT_VALUE) {
		pr_err("[CVM ENCLAVE] handle=%lu run returned error=%ld value=%ld after %u resumes\n",
		       test->handle, ret.error, ret.value, resumes);
		return -EINVAL;
	}
	pr_info("[CVM ENCLAVE] PASS: nested enclave handle=%lu executed and exited value=%ld resumes=%u\n",
		test->handle, ret.value, resumes);
	return 0;
}

static long run_enclave_migration_transition(void *data)
{
	struct enclave_migration_transition *transition = data;
	unsigned long irq_flags_before;
	unsigned long irq_flags_after;
	struct sbiret timer_ret;
	u64 timer_delta;

	if (raw_smp_processor_id() != transition->expected_cpu) {
		transition->error = -EXDEV;
		return transition->error;
	}
	if (!kernel_fpu_available()) {
		transition->error = -EOPNOTSUPP;
		return transition->error;
	}

	/*
	 * Arm an explicit near-term timer for the initial run.  Depending on the
	 * phase of the periodic Linux tick made migration probabilistic on fast
	 * hardware: the enclave could finish before a pending tick and never need
	 * to resume on CPU1.  The long payload loop keeps it active beyond this
	 * two-millisecond deadline.  The resulting timer interrupt also makes the
	 * normal clockevent path program Linux's following deadline.
	 */
	if (transition->fid == SBI_SM_RUN_ENCLAVE) {
		timer_delta = riscv_timebase / 500;
		if (!timer_delta)
			timer_delta = 1;
		timer_ret = sbi_ecall(SBI_EXT_TIME, SBI_EXT_TIME_SET_TIMER,
				      get_cycles64() + timer_delta,
				      0, 0, 0, 0, 0);
		if (timer_ret.error) {
			transition->error = timer_ret.error;
			return transition->error;
		}
	}

	kernel_fpu_begin();
	install_parent_fp_guard(transition->expected_parent_fp,
				 transition->expected_parent_fcsr);
	local_save_flags(irq_flags_before);
	transition->ret = enclave_ecall(transition->fid,
					 transition->test->handle);
	if (!parent_fp_guard_matches(transition->expected_parent_fp,
				     transition->expected_parent_fcsr,
				     &transition->actual_parent_fp,
				     &transition->actual_parent_fcsr))
		transition->error = -EKEYREJECTED;
	local_save_flags(irq_flags_after);
	kernel_fpu_end();

	if (!transition->error &&
	    irqs_disabled_flags(irq_flags_before) !=
	    irqs_disabled_flags(irq_flags_after))
		transition->error = -EINVAL;
	return transition->error;
}

static int run_test_enclave_migrating(struct test_enclave *test)
{
	unsigned long fid = SBI_SM_RUN_ENCLAVE;
	unsigned int transitions = 0;
	unsigned int visited_cpus = 0;
	struct sbiret ret = { .error = -1 };

	if (!cpu_online(0) || !cpu_online(1))
		return -EINVAL;

	do {
		struct enclave_migration_transition transition = {
			.test = test,
			.fid = fid,
			.expected_cpu = transitions & 1,
			.expected_parent_fp = TEST_PARENT_FP_PATTERN_BASE +
					      0x100 + (transitions & 1),
			.expected_parent_fcsr = TEST_PARENT_FCSR_PATTERN_BASE +
						0x08 + (transitions & 1),
		};
		long error;

		error = work_on_cpu(transition.expected_cpu,
				    run_enclave_migration_transition,
				    &transition);
		if (error || transition.error) {
			pr_err("[CVM ENCLAVE] CPU%u migration transition failed: work=%ld error=%d parent=%llx/%llx expected=%llx/%llx\n",
			       transition.expected_cpu, error, transition.error,
			       transition.actual_parent_fp,
			       transition.actual_parent_fcsr,
			       transition.expected_parent_fp,
			       transition.expected_parent_fcsr);
			return -EINVAL;
		}

		ret = transition.ret;
		visited_cpus |= BIT(transition.expected_cpu);
		transitions++;
		if (ret.error != SM_ENCLAVE_INTERRUPTED)
			break;
		fid = SBI_SM_RESUME_ENCLAVE;
	} while (transitions < TEST_MAX_RESUMES);

	if (ret.error || ret.value != TEST_EXIT_VALUE ||
	    visited_cpus != (BIT(0) | BIT(1))) {
		pr_err("[CVM ENCLAVE] migrated handle=%lu returned error=%ld value=%ld transitions=%u cpus=0x%x\n",
		       test->handle, ret.error, ret.value, transitions,
		       visited_cpus);
		return -EINVAL;
	}

	pr_info("[CVM ENCLAVE] PASS: one enclave resumed across CPU0/CPU1 with parent FP/FCSR guards intact\n");
	return 0;
}

static int enclave_run_racer_fn(void *data)
{
	struct enclave_run_racer *racer = data;

	complete(&racer->ready);
	wait_for_completion(racer->go);
	racer->error = run_test_enclave(racer->test);
	complete(&racer->done);
	return 0;
}

static int run_test_enclaves_concurrently(void)
{
	struct completion go;
	struct enclave_run_racer racers[2];
	struct task_struct *tasks[2];

	if (!cpu_online(0) || !cpu_online(1))
		return -EINVAL;
	init_completion(&go);
	for (int i = 0; i < ARRAY_SIZE(racers); i++) {
		racers[i].test = &test_enclaves[i];
		racers[i].go = &go;
		racers[i].error = -1;
		init_completion(&racers[i].ready);
		init_completion(&racers[i].done);
		tasks[i] = kthread_create(enclave_run_racer_fn, &racers[i],
					  "cvm-encl-run/%d", i);
		if (IS_ERR(tasks[i])) {
			if (i == 1) {
				complete_all(&go);
				wait_for_completion(&racers[0].done);
			}
			return -ENOMEM;
		}
		kthread_bind(tasks[i], i);
		wake_up_process(tasks[i]);
	}
	wait_for_completion(&racers[0].ready);
	wait_for_completion(&racers[1].ready);
	complete_all(&go);
	wait_for_completion(&racers[0].done);
	wait_for_completion(&racers[1].done);

	if (racers[0].error || racers[1].error) {
		pr_err("[CVM ENCLAVE] concurrent enclave runs returned %d, %d\n",
		       racers[0].error, racers[1].error);
		return -EINVAL;
	}
	pr_info("[CVM ENCLAVE] PASS: two enclaves executed concurrently on CPU0/CPU1\n");
	pr_info("[CVM ENCLAVE] PASS: concurrent parent CVM FP/FCSR guards survived nested transitions\n");
	return 0;
}

static int validate_measurement_sealing_isolation(void)
{
	struct test_sealing_key *first =
		(struct test_sealing_key *)(page_address(test_enclaves[0].utm) +
					    TEST_SEAL1_OFFSET);
	struct test_sealing_key *second =
		(struct test_sealing_key *)(page_address(test_enclaves[1].utm) +
					    TEST_SEAL1_OFFSET);
	u64 first_random = *(u64 *)(page_address(test_enclaves[0].utm) +
				      TEST_RANDOM1_OFFSET);
	u64 second_random = *(u64 *)(page_address(test_enclaves[1].utm) +
				       TEST_RANDOM1_OFFSET);
	u64 first_fp = *(u64 *)(page_address(test_enclaves[0].utm) +
				 TEST_FP_AFTER_SERVICES_OFFSET);
	u64 second_fp = *(u64 *)(page_address(test_enclaves[1].utm) +
				  TEST_FP_AFTER_SERVICES_OFFSET);
	u64 first_fcsr = *(u64 *)(page_address(test_enclaves[0].utm) +
				   TEST_FCSR_AFTER_SERVICES_OFFSET);
	u64 second_fcsr = *(u64 *)(page_address(test_enclaves[1].utm) +
				    TEST_FCSR_AFTER_SERVICES_OFFSET);

	if (!memcmp(test_enclaves[0].expected_measurement,
		    test_enclaves[1].expected_measurement, TEST_MD_SIZE)) {
		pr_err("[CVM ENCLAVE] measurement variants produced one digest\n");
		return -EINVAL;
	}
	if (!memcmp(first->key, second->key, sizeof(first->key))) {
		pr_err("[CVM ENCLAVE] distinct measurements produced one sealing key\n");
		return -EKEYREJECTED;
	}
	if (test_enclaves[0].expected_fcsr == test_enclaves[1].expected_fcsr ||
	    first_fcsr != test_enclaves[0].expected_fcsr ||
	    second_fcsr != test_enclaves[1].expected_fcsr ||
	    first_fcsr == second_fcsr) {
		pr_err("[CVM ENCLAVE] concurrent FCSR guards are not isolated: %llx/%llx expected %llx/%llx\n",
		       first_fcsr, second_fcsr,
		       test_enclaves[0].expected_fcsr,
		       test_enclaves[1].expected_fcsr);
		return -EKEYREJECTED;
	}
	if (first_random == second_random) {
		pr_err("[CVM ENCLAVE] random counter repeated across enclaves\n");
		return -EKEYREJECTED;
	}
	if (test_enclaves[0].expected_fp == test_enclaves[1].expected_fp ||
	    first_fp != test_enclaves[0].expected_fp ||
	    second_fp != test_enclaves[1].expected_fp || first_fp == second_fp) {
		pr_err("[CVM ENCLAVE] concurrent FP guards are not isolated: %llx/%llx expected %llx/%llx\n",
		       first_fp, second_fp, test_enclaves[0].expected_fp,
		       test_enclaves[1].expected_fp);
		return -EKEYREJECTED;
	}
	pr_info("[CVM ENCLAVE] PASS: fresh and concurrent FP/FCSR state survived timer and service transitions\n");
	pr_info("[CVM ENCLAVE] PASS: sealing keys isolated by identity and measurement under KDF v1\n");
	return 0;
}

static int __init cvm_enclave_lifecycle_init(void)
{
	struct sbiret ret;
	u8 baseline_measurement[TEST_MD_SIZE];
	u8 baseline_sealing_key[TEST_SEALING_KEY_SIZE];
	struct test_sealing_key *recreated_seal;
	unsigned long stale_handle;

	if (!attest_challenge || !fresh_attest_challenge ||
	    attest_challenge == fresh_attest_challenge) {
		pr_err("[CVM ENCLAVE] two distinct nonzero verifier challenges are required\n");
		return -EINVAL;
	}

	if (test_cvm_management_access_control() ||
	    test_invalid_create_args())
		goto failed;

	if (create_test_enclave(&test_enclaves[0], 0) ||
	    create_test_enclave(&test_enclaves[1], 1))
		goto failed;

	/* Handles include a generation above the low eight slot bits.  A previous
	 * negative run in the same CVM may have advanced that generation without
	 * changing the allocator slots used by this pair. */
	if ((test_enclaves[0].handle & TEST_HANDLE_SLOT_MASK) != 0 ||
	    (test_enclaves[1].handle & TEST_HANDLE_SLOT_MASK) != 1) {
		pr_err("[CVM ENCLAVE] unexpected local handles: %lu, %lu\n",
		       test_enclaves[0].handle, test_enclaves[1].handle);
		goto failed;
	}
	stale_handle = test_enclaves[0].handle;

	ret = enclave_ecall(SBI_SM_DESTROY_ENCLAVE, 7);
	if (ret.error != SM_ENCLAVE_NOT_DESTROYABLE) {
		pr_err("[CVM ENCLAVE] invalid handle destroy returned %ld\n",
		       ret.error);
		goto failed;
	}

	if (test_random_access_control() ||
	    run_test_enclaves_concurrently() ||
	    validate_attestation_outputs(&test_enclaves[0]) ||
	    validate_attestation_outputs(&test_enclaves[1]) ||
	    validate_measurement_sealing_isolation())
		goto failed;
	memcpy(baseline_measurement, test_enclaves[0].expected_measurement,
	       sizeof(baseline_measurement));
	memcpy(baseline_sealing_key,
	       ((struct test_sealing_key *)(page_address(test_enclaves[0].utm) +
					    TEST_SEAL1_OFFSET))->key,
	       sizeof(baseline_sealing_key));

	ret = enclave_ecall(SBI_SM_RESUME_ENCLAVE,
			    test_enclaves[0].handle);
	if (ret.error != SM_ENCLAVE_NOT_RESUMABLE) {
		pr_err("[CVM ENCLAVE] resume after clean exit returned %ld\n",
		       ret.error);
		goto failed;
	}
	ret = enclave_ecall(SBI_SM_RUN_ENCLAVE, test_enclaves[0].handle);
	if (ret.error != SM_ENCLAVE_NOT_FRESH) {
		pr_err("[CVM ENCLAVE] second run returned %ld\n", ret.error);
		goto failed;
	}
	ret = enclave_ecall(SBI_SM_RESUME_ENCLAVE, 7);
	if (ret.error != SM_ENCLAVE_NOT_RUNNABLE) {
		pr_err("[CVM ENCLAVE] invalid handle resume returned %ld\n",
		       ret.error);
		goto failed;
	}
	pr_info("[CVM ENCLAVE] PASS: terminal and invalid resumes rejected\n");

	if (destroy_test_enclave(&test_enclaves[0]) ||
	    destroy_test_enclave(&test_enclaves[1]))
		goto failed;

	if (create_test_enclave(&test_enclaves[0], 0))
		goto failed;
	if (test_enclaves[0].handle == stale_handle) {
		pr_err("[CVM ENCLAVE] recycled slot reused stale handle %lu\n",
		       test_enclaves[0].handle);
		goto failed;
	}
	ret = enclave_ecall(SBI_SM_RUN_ENCLAVE, stale_handle);
	if (ret.error != SM_ENCLAVE_NOT_RUNNABLE) {
		pr_err("[CVM ENCLAVE] stale handle run returned %ld\n",
		       ret.error);
		goto failed;
	}
	ret = enclave_ecall(SBI_SM_RESUME_ENCLAVE, stale_handle);
	if (ret.error != SM_ENCLAVE_NOT_RUNNABLE) {
		pr_err("[CVM ENCLAVE] stale handle resume returned %ld\n",
		       ret.error);
		goto failed;
	}
	ret = enclave_ecall(SBI_SM_DESTROY_ENCLAVE, stale_handle);
	if (ret.error != SM_ENCLAVE_NOT_DESTROYABLE) {
		pr_err("[CVM ENCLAVE] stale handle destroy returned %ld\n",
		       ret.error);
		goto failed;
	}
#if BITS_PER_LONG > 32
	ret = enclave_ecall(SBI_SM_RUN_ENCLAVE,
			    test_enclaves[0].handle | (1UL << 32));
	if (ret.error != SM_ENCLAVE_NOT_RUNNABLE) {
		pr_err("[CVM ENCLAVE] noncanonical handle run returned %ld\n",
		       ret.error);
		goto failed;
	}
	ret = enclave_ecall(SBI_SM_DESTROY_ENCLAVE,
			    test_enclaves[0].handle | (1UL << 32));
	if (ret.error != SM_ENCLAVE_NOT_DESTROYABLE) {
		pr_err("[CVM ENCLAVE] noncanonical handle destroy returned %ld\n",
		       ret.error);
		goto failed;
	}
#endif
	pr_info("[CVM ENCLAVE] PASS: stale and noncanonical enclave handles rejected after slot reuse\n");
	if (memcmp(baseline_measurement,
		   test_enclaves[0].expected_measurement,
		   sizeof(baseline_measurement))) {
		pr_err("[CVM ENCLAVE] measurement changed across slot reuse\n");
		goto failed;
	}
	if (run_test_enclave_migrating(&test_enclaves[0]) ||
	    validate_attestation_outputs(&test_enclaves[0]))
		goto failed;
	recreated_seal = (struct test_sealing_key *)(
		page_address(test_enclaves[0].utm) + TEST_SEAL1_OFFSET);
	if (memcmp(baseline_sealing_key, recreated_seal->key,
		   sizeof(baseline_sealing_key))) {
		pr_err("[CVM ENCLAVE] sealing key changed across slot reuse\n");
		goto failed;
	}
	pr_info("[CVM ENCLAVE] PASS: identical enclave image retained its measurement and sealing key across reuse\n");
	if (race_destroy_test_enclave(&test_enclaves[0]))
		goto failed;

	ret = enclave_ecall(SBI_SM_DESTROY_ENCLAVE,
			    test_enclaves[0].handle);
	if (ret.error != SM_ENCLAVE_NOT_DESTROYABLE) {
		pr_err("[CVM ENCLAVE] duplicate destroy returned %ld\n",
		       ret.error);
		goto failed;
	}

	memzero_explicit(baseline_sealing_key,
			 sizeof(baseline_sealing_key));
	pr_info("[CVM ENCLAVE] PASS: nested create/destroy lifecycle\n");
	return 0;

failed:
	memzero_explicit(baseline_sealing_key,
			 sizeof(baseline_sealing_key));
	for (size_t i = 0; i < ARRAY_SIZE(test_enclaves); i++) {
		if (test_enclaves[i].epm) {
			enclave_ecall(SBI_SM_DESTROY_ENCLAVE,
				      test_enclaves[i].handle);
			release_test_memory(&test_enclaves[i]);
		}
	}
	return -EINVAL;
}

static void __exit cvm_enclave_lifecycle_exit(void)
{
}

module_init(cvm_enclave_lifecycle_init);
module_exit(cvm_enclave_lifecycle_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Zion CVM nested enclave execution and lifecycle regression");
