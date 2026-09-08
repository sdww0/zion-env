/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef CVM_ED25519_H
#define CVM_ED25519_H

#include <linux/types.h>

bool cvm_ed25519_verify(const u8 signature[64], const void *message,
			size_t message_len, const u8 public_key[32]);

#endif
