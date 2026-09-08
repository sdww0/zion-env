// SPDX-License-Identifier: GPL-2.0-only

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define CVM_MANAGEMENT_RUN _IO('Z', 1)
#define CVM_MANAGEMENT_CAPACITY _IOW('Z', 2, unsigned long)

static int open_device(void)
{
	int fd = open("/dev/cvm-management-negative", O_RDWR);

	if (fd < 0) {
		perror("cvm-management-control: open");
		return -1;
	}
	return fd;
}

int main(int argc, char **argv)
{
	unsigned char stash[2] = { 0x5a, 0xa5 };
	int fd;

	if (argc == 3 && !strcmp(argv[1], "capacity")) {
		char *end = NULL;
		unsigned long expected = strtoul(argv[2], &end, 0);

		if (!end || *end) {
			fprintf(stderr, "cvm-management-control: invalid capacity\n");
			return 1;
		}
		fd = open_device();
		if (fd < 0)
			return 1;
		if (ioctl(fd, CVM_MANAGEMENT_CAPACITY, expected) < 0) {
			perror("cvm-management-control: capacity");
			close(fd);
			return 1;
		}
		close(fd);
		printf("[CVM MANAGEMENT] PASS: %lu scratch CVM slots created and capacity overflow rejected\n",
		       expected);
		return 0;
	}
	if (argc != 1) {
		fprintf(stderr, "usage: cvm-management-control [capacity N]\n");
		return 1;
	}

	fd = open_device();
	if (fd < 0)
		return 1;
	if (ioctl(fd, CVM_MANAGEMENT_RUN, stash) < 0) {
		perror("cvm-management-control: regression");
		close(fd);
		return 1;
	}
	close(fd);
	puts("[CVM MANAGEMENT] PASS: protected mappings, page-table injection, non-leaf load, and overflow rejected");
	return 0;
}
