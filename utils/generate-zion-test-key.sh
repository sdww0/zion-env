#!/usr/bin/env bash

set -euo pipefail

readonly ROOT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
readonly OUTPUT_HEADER="${1:?usage: $0 OUTPUT_HEADER PUBLIC_KEY [VERIFIER_HEADER]}"
readonly PUBLIC_KEY="${2:?usage: $0 OUTPUT_HEADER PUBLIC_KEY [VERIFIER_HEADER]}"
readonly VERIFIER_HEADER="${3:-}"
readonly WORK_DIR="$(mktemp -d)"

cleanup()
{
	rm -rf "${WORK_DIR}"
}
trap cleanup EXIT

mkdir -p "$(dirname -- "${OUTPUT_HEADER}")" "$(dirname -- "${PUBLIC_KEY}")"

cat > "${WORK_DIR}/generate.c" <<'EOF'
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

void ed25519_create_keypair(unsigned char *public_key,
			    unsigned char *private_key,
			    const unsigned char *seed);

static void read_seed(unsigned char seed[32])
{
	int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
	size_t done = 0;

	if (fd < 0) {
		perror("open /dev/urandom");
		exit(EXIT_FAILURE);
	}
	while (done < 32) {
		ssize_t ret = read(fd, seed + done, 32 - done);

		if (ret > 0) {
			done += (size_t)ret;
			continue;
		}
		if (ret < 0 && errno == EINTR)
			continue;
		perror("read /dev/urandom");
		exit(EXIT_FAILURE);
	}
	close(fd);
}

static void emit_array(FILE *file, const char *name,
		       const unsigned char *value, size_t size)
{
	fprintf(file, "static const unsigned char %s[] = {\n", name);
	for (size_t i = 0; i < size; i++) {
		if (!(i % 8))
			fputs("\t", file);
		fprintf(file, "0x%02x%s", value[i], i + 1 == size ? "" : ", ");
		if (i + 1 == size || i % 8 == 7)
			fputc('\n', file);
	}
	fputs("};\n", file);
}

int main(int argc, char **argv)
{
	unsigned char seed[32];
	unsigned char private_key[64];
	unsigned char public_key[32];
	FILE *header;
	FILE *public;
	FILE *verifier;

	if (argc != 3 && argc != 4)
		return EXIT_FAILURE;
	read_seed(seed);
	ed25519_create_keypair(public_key, private_key, seed);

	header = fopen(argv[1], "w");
	if (!header) {
		perror("fopen output header");
		return EXIT_FAILURE;
	}
	fputs("/* Generated ephemeral TEST identity. Do not deploy. */\n"
	      "#warning Using an ephemeral TEST device key. No production security guarantee.\n",
	      header);
	emit_array(header, "_sanctum_dev_secret_key", private_key,
		   sizeof(private_key));
	fputs("static const size_t _sanctum_dev_secret_key_len = 64;\n\n",
	      header);
	emit_array(header, "_sanctum_dev_public_key", public_key,
		   sizeof(public_key));
	fputs("static const size_t _sanctum_dev_public_key_len = 32;\n",
	      header);
	if (fclose(header)) {
		perror("fclose output header");
		return EXIT_FAILURE;
	}

	public = fopen(argv[2], "wb");
	if (!public) {
		perror("fopen public key");
		return EXIT_FAILURE;
	}
	if (fwrite(public_key, 1, sizeof(public_key), public) !=
	    sizeof(public_key) || fclose(public)) {
		perror("write public key");
		return EXIT_FAILURE;
	}

	if (argc == 4) {
		verifier = fopen(argv[3], "w");
		if (!verifier) {
			perror("fopen verifier header");
			return EXIT_FAILURE;
		}
		fputs("#ifndef _TEST_DEV_KEY_H_\n"
		      "#define _TEST_DEV_KEY_H_\n\n"
		      "#include <stddef.h>\n\n"
		      "#warning Using an ephemeral TEST device public key.\n",
		      verifier);
		emit_array(verifier, "_sanctum_dev_public_key", public_key,
			   sizeof(public_key));
		fputs("static const size_t _sanctum_dev_public_key_len = 32;\n\n"
		      "#endif /* _TEST_DEV_KEY_H_ */\n", verifier);
		if (fclose(verifier)) {
			perror("fclose verifier header");
			return EXIT_FAILURE;
		}
	}
	return EXIT_SUCCESS;
}
EOF

"${HOSTCC:-cc}" -O2 -D__riscv_xlen=64 \
	-I"${ROOT_DIR}/opensbi/include" \
	-I"${ROOT_DIR}/opensbi/zion/src/ed25519" \
	-I"${ROOT_DIR}/opensbi/zion/src/sha3" \
	"${WORK_DIR}/generate.c" \
	"${ROOT_DIR}/opensbi/zion/src/ed25519/keypair.c" \
	"${ROOT_DIR}/opensbi/zion/src/ed25519/ge.c" \
	"${ROOT_DIR}/opensbi/zion/src/ed25519/fe.c" \
	"${ROOT_DIR}/opensbi/zion/src/sha3/sha3.c" \
	-o "${WORK_DIR}/generate"

if [ -n "${VERIFIER_HEADER}" ]; then
	mkdir -p "$(dirname -- "${VERIFIER_HEADER}")"
	"${WORK_DIR}/generate" "${OUTPUT_HEADER}" "${PUBLIC_KEY}" \
		"${VERIFIER_HEADER}"
	chmod 0644 "${VERIFIER_HEADER}"
else
	"${WORK_DIR}/generate" "${OUTPUT_HEADER}" "${PUBLIC_KEY}"
fi
chmod 0600 "${OUTPUT_HEADER}"
sha256sum "${PUBLIC_KEY}" > "${PUBLIC_KEY}.sha256"
