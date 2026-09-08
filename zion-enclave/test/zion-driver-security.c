// SPDX-License-Identifier: Apache-2.0
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#define ZION_IOC_MAGIC 0xa4

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

#define ZION_IOC_CREATE_ENCLAVE \
	_IOR(ZION_IOC_MAGIC, 0x00, struct zion_ioctl_create_enclave)
#define ZION_IOC_DESTROY_ENCLAVE \
	_IOW(ZION_IOC_MAGIC, 0x01, struct zion_ioctl_create_enclave)
#define ZION_IOC_UTM_INIT \
	_IOR(ZION_IOC_MAGIC, 0x07, struct zion_ioctl_create_enclave)

static int failures;

static void expect_errno(const char *name, int result, int expected)
{
	if (result == -1 && errno == expected) {
		printf("[DRIVER SECURITY] PASS: %s\n", name);
		return;
	}
	printf("[DRIVER SECURITY] FAIL: %s result=%d errno=%d expected=%d\n",
	       name, result, errno, expected);
	failures++;
}

int main(void)
{
	struct zion_ioctl_create_enclave request;
	struct zion_ioctl_create_enclave create;
	struct zion_ioctl_create_enclave *fault_request;
	void *mapping;
	void *ioctl_page;
	int owner;
	int stranger;
	int result;

	owner = open("/dev/zion_enclave", O_RDWR | O_CLOEXEC);
	stranger = open("/dev/zion_enclave", O_RDWR | O_CLOEXEC);
	if (owner < 0 || stranger < 0) {
		perror("open /dev/zion_enclave");
		return 1;
	}
	ioctl_page = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
			  MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (ioctl_page == MAP_FAILED) {
		perror("mmap ioctl page");
		return 1;
	}
	fault_request = ioctl_page;

	memset(&request, 0, sizeof(request));
	errno = 0;
	expect_errno("zero-page EPM rejected",
		     ioctl(owner, ZION_IOC_CREATE_ENCLAVE, &request), EINVAL);

	memset(&request, 0, sizeof(request));
	request.min_pages = UINTPTR_MAX;
	errno = 0;
	expect_errno("oversized EPM rejected",
		     ioctl(owner, ZION_IOC_CREATE_ENCLAVE, &request), EINVAL);

	memset(&create, 0, sizeof(create));
	create.min_pages = 16;
	if (ioctl(owner, ZION_IOC_CREATE_ENCLAVE, &create)) {
		perror("create valid enclave");
		return 1;
	}
	printf("[DRIVER SECURITY] PASS: valid EPM created\n");

	memset(&request, 0, sizeof(request));
	request.min_pages = 16;
	errno = 0;
	expect_errno("second CREATE on one file rejected",
		     ioctl(owner, ZION_IOC_CREATE_ENCLAVE, &request), EBUSY);

	memset(&request, 0, sizeof(request));
	request.eid = create.eid;
	request.utm_size = 4096;
	errno = 0;
	expect_errno("cross-file enclave access rejected",
		     ioctl(stranger, ZION_IOC_UTM_INIT, &request), EPERM);

	errno = 0;
	mapping = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED,
		       owner, (off_t)create.epm_size);
	if (mapping == MAP_FAILED && errno == EINVAL) {
		printf("[DRIVER SECURITY] PASS: out-of-EPM mmap rejected\n");
	} else {
		printf("[DRIVER SECURITY] FAIL: out-of-EPM mmap accepted errno=%d\n",
		       errno);
		failures++;
		if (mapping != MAP_FAILED)
			munmap(mapping, 4096);
	}

	mapping = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, owner, 0);
	if (mapping == MAP_FAILED) {
		perror("valid EPM mmap");
		failures++;
	} else {
		*(volatile unsigned long *)mapping = 0x5a5aa5a5UL;
		printf("[DRIVER SECURITY] PASS: bounded EPM mmap accepted\n");
	}

	memset(&request, 0, sizeof(request));
	request.eid = create.eid;
	errno = 0;
	expect_errno("zero-size UTM rejected",
		     ioctl(owner, ZION_IOC_UTM_INIT, &request), EINVAL);

	request.utm_size = (uintptr_t)1024 * 1024 * 1024 + 4096;
	errno = 0;
	expect_errno("oversized UTM rejected",
		     ioctl(owner, ZION_IOC_UTM_INIT, &request), EINVAL);

	memset(fault_request, 0, sizeof(*fault_request));
	fault_request->eid = create.eid;
	/* Three pages require a four-page buddy allocation.  The extra allocator
	 * page must remain an internal detail rather than changing the ABI size. */
	fault_request->utm_size = 3 * 4096;
	if (mprotect(ioctl_page, 4096, PROT_READ)) {
		perror("mprotect ioctl page read-only");
		return 1;
	}
	errno = 0;
	expect_errno("UTM copy-out failure reported",
		     ioctl(owner, ZION_IOC_UTM_INIT, fault_request), EFAULT);
	if (mprotect(ioctl_page, 4096, PROT_READ | PROT_WRITE)) {
		perror("mprotect ioctl page writable");
		return 1;
	}
	if (ioctl(owner, ZION_IOC_UTM_INIT, fault_request)) {
		perror("valid UTM init");
		failures++;
	} else {
		printf("[DRIVER SECURITY] PASS: UTM copy-out rollback permits retry\n");
	}

	errno = 0;
	expect_errno("duplicate UTM rejected",
		     ioctl(owner, ZION_IOC_UTM_INIT, fault_request), EBUSY);

	memset(&request, 0, sizeof(request));
	request.eid = create.eid;
	result = ioctl(owner, ZION_IOC_DESTROY_ENCLAVE, &request);
	if (result) {
		perror("destroy enclave");
		failures++;
	} else {
		printf("[DRIVER SECURITY] PASS: enclave destroyed\n");
	}

	/* The VMA owns a reference, so destroying the handle must not recycle its
	 * backing pages until this mapping is closed. */
	if (mapping != MAP_FAILED) {
		if (*(volatile unsigned long *)mapping != 0x5a5aa5a5UL) {
			printf("[DRIVER SECURITY] FAIL: mapped EPM changed after destroy\n");
			failures++;
		} else {
			printf("[DRIVER SECURITY] PASS: VMA lifetime pins EPM\n");
		}
		munmap(mapping, 4096);
	}

	/* DESTROY is input-only.  A read-only request must not turn a successful
	 * destroy into EFAULT through an unnecessary copy_to_user(). */
	memset(&create, 0, sizeof(create));
	create.min_pages = 16;
	if (ioctl(owner, ZION_IOC_CREATE_ENCLAVE, &create)) {
		perror("create input-only destroy enclave");
		failures++;
	} else {
		memset(fault_request, 0, sizeof(*fault_request));
		fault_request->eid = create.eid;
		if (mprotect(ioctl_page, 4096, PROT_READ)) {
			perror("mprotect destroy request");
			return 1;
		}
		if (ioctl(owner, ZION_IOC_DESTROY_ENCLAVE, fault_request)) {
			perror("read-only destroy request");
			failures++;
		} else {
			printf("[DRIVER SECURITY] PASS: input-only destroy skips copy-out\n");
		}
		mprotect(ioctl_page, 4096, PROT_READ | PROT_WRITE);
	}

	munmap(ioctl_page, 4096);
	close(stranger);
	close(owner);
	if (failures)
		return 1;
	printf("[DRIVER SECURITY] PASS: all negative ABI tests\n");
	return 0;
}
