// SPDX-License-Identifier: GPL-2.0-only
/*
 * Verification-only Ed25519 group arithmetic adapted from the public-domain
 * TweetNaCl implementation. Zion/Zion hashes Ed25519 transcripts with
 * SHA3-512 rather than the RFC 8032 SHA-512 construction, so the kernel's
 * standard signature algorithms cannot verify these reports.
 */
#include <crypto/hash.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/string.h>

#include "cvm-ed25519.h"

typedef s64 gf[16];

static const gf gf0;
static const gf gf1 = { 1 };
static const gf ed_d = {
	0x78a3, 0x1359, 0x4dca, 0x75eb,
	0xd8ab, 0x4141, 0x0a4d, 0x0070,
	0xe898, 0x7779, 0x4079, 0x8cc7,
	0xfe73, 0x2b6f, 0x6cee, 0x5203,
};
static const gf ed_d2 = {
	0xf159, 0x26b2, 0x9b94, 0xebd6,
	0xb156, 0x8283, 0x149a, 0x00e0,
	0xd130, 0xeef3, 0x80f2, 0x198e,
	0xfce7, 0x56df, 0xd9dc, 0x2406,
};
static const gf ed_x = {
	0xd51a, 0x8f25, 0x2d60, 0xc956,
	0xa7b2, 0x9525, 0xc760, 0x692c,
	0xdc5c, 0xfdd6, 0xe231, 0xc0a4,
	0x53fe, 0xcd6e, 0x36d3, 0x2169,
};
static const gf ed_y = {
	0x6658, 0x6666, 0x6666, 0x6666,
	0x6666, 0x6666, 0x6666, 0x6666,
	0x6666, 0x6666, 0x6666, 0x6666,
	0x6666, 0x6666, 0x6666, 0x6666,
};
static const gf sqrt_m1 = {
	0xa0b0, 0x4a0e, 0x1b27, 0xc4ee,
	0xe478, 0xad2f, 0x1806, 0x2f43,
	0xd7a7, 0x3dfb, 0x0099, 0x2b4d,
	0xdf0b, 0x4fc1, 0x2480, 0x2b83,
};
static const u8 group_order[32] = {
	0xed, 0xd3, 0xf5, 0x5c, 0x1a, 0x63, 0x12, 0x58,
	0xd6, 0x9c, 0xf7, 0xa2, 0xde, 0xf9, 0xde, 0x14,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10,
};

static void set25519(gf r, const gf a)
{
	unsigned int i;

	for (i = 0; i < 16; i++)
		r[i] = a[i];
}

static void car25519(gf o)
{
	s64 c;
	unsigned int i;

	for (i = 0; i < 16; i++) {
		o[i] += 1LL << 16;
		c = o[i] >> 16;
		o[(i + 1) * (i < 15)] += c - 1 + 37 * (c - 1) * (i == 15);
		o[i] -= c * (1LL << 16);
	}
}

static void sel25519(gf p, gf q, unsigned int b)
{
	s64 mask = ~((s64)b - 1);
	s64 t;
	unsigned int i;

	for (i = 0; i < 16; i++) {
		t = mask & (p[i] ^ q[i]);
		p[i] ^= t;
		q[i] ^= t;
	}
}

static void pack25519(u8 out[32], const gf input)
{
	gf m;
	gf t;
	s64 b;
	unsigned int i;
	unsigned int j;

	set25519(t, input);
	car25519(t);
	car25519(t);
	car25519(t);
	for (j = 0; j < 2; j++) {
		m[0] = t[0] - 0xffed;
		for (i = 1; i < 15; i++) {
			m[i] = t[i] - 0xffff - ((m[i - 1] >> 16) & 1);
			m[i - 1] &= 0xffff;
		}
		m[15] = t[15] - 0x7fff - ((m[14] >> 16) & 1);
		b = (m[15] >> 16) & 1;
		m[14] &= 0xffff;
		sel25519(t, m, 1 - b);
	}
	for (i = 0; i < 16; i++) {
		out[2 * i] = t[i] & 0xff;
		out[2 * i + 1] = t[i] >> 8;
	}
}

static void unpack25519(gf out, const u8 input[32])
{
	unsigned int i;

	for (i = 0; i < 16; i++)
		out[i] = input[2 * i] + ((s64)input[2 * i + 1] << 8);
	out[15] &= 0x7fff;
}

static int neq25519(const gf a, const gf b)
{
	u8 packed_a[32];
	u8 packed_b[32];

	pack25519(packed_a, a);
	pack25519(packed_b, b);
	return memcmp(packed_a, packed_b, sizeof(packed_a));
}

static unsigned int par25519(const gf a)
{
	u8 packed[32];

	pack25519(packed, a);
	return packed[0] & 1;
}

static void field_add(gf out, const gf a, const gf b)
{
	unsigned int i;

	for (i = 0; i < 16; i++)
		out[i] = a[i] + b[i];
}

static void field_sub(gf out, const gf a, const gf b)
{
	unsigned int i;

	for (i = 0; i < 16; i++)
		out[i] = a[i] - b[i];
}

static void field_mul(gf out, const gf a, const gf b)
{
	s64 t[31] = { 0 };
	unsigned int i;
	unsigned int j;

	for (i = 0; i < 16; i++)
		for (j = 0; j < 16; j++)
			t[i + j] += a[i] * b[j];
	for (i = 0; i < 15; i++)
		t[i] += 38 * t[i + 16];
	for (i = 0; i < 16; i++)
		out[i] = t[i];
	car25519(out);
	car25519(out);
}

static void field_square(gf out, const gf a)
{
	field_mul(out, a, a);
}

static void inv25519(gf out, const gf input)
{
	gf c;
	int i;

	set25519(c, input);
	for (i = 253; i >= 0; i--) {
		field_square(c, c);
		if (i != 2 && i != 4)
			field_mul(c, c, input);
	}
	set25519(out, c);
}

static void pow2523(gf out, const gf input)
{
	gf c;
	int i;

	set25519(c, input);
	for (i = 250; i >= 0; i--) {
		field_square(c, c);
		if (i != 1)
			field_mul(c, c, input);
	}
	set25519(out, c);
}

static void point_add(gf p[4], gf q[4])
{
	gf a;
	gf b;
	gf c;
	gf d;
	gf e;
	gf f;
	gf g;
	gf h;
	gf t;

	field_sub(a, p[1], p[0]);
	field_sub(t, q[1], q[0]);
	field_mul(a, a, t);
	field_add(b, p[0], p[1]);
	field_add(t, q[0], q[1]);
	field_mul(b, b, t);
	field_mul(c, p[3], q[3]);
	field_mul(c, c, ed_d2);
	field_mul(d, p[2], q[2]);
	field_add(d, d, d);
	field_sub(e, b, a);
	field_sub(f, d, c);
	field_add(g, d, c);
	field_add(h, b, a);
	field_mul(p[0], e, f);
	field_mul(p[1], h, g);
	field_mul(p[2], g, f);
	field_mul(p[3], e, h);
}

static void point_cswap(gf p[4], gf q[4], unsigned int b)
{
	unsigned int i;

	for (i = 0; i < 4; i++)
		sel25519(p[i], q[i], b);
}

static void point_pack(u8 out[32], gf p[4])
{
	gf tx;
	gf ty;
	gf zi;

	inv25519(zi, p[2]);
	field_mul(tx, p[0], zi);
	field_mul(ty, p[1], zi);
	pack25519(out, ty);
	out[31] ^= par25519(tx) << 7;
}

static void point_scalarmult(gf p[4], gf q[4], const u8 scalar[32])
{
	int i;
	unsigned int bit;

	set25519(p[0], gf0);
	set25519(p[1], gf1);
	set25519(p[2], gf1);
	set25519(p[3], gf0);
	for (i = 255; i >= 0; i--) {
		bit = (scalar[i >> 3] >> (i & 7)) & 1;
		point_cswap(p, q, bit);
		point_add(q, p);
		point_add(p, p);
		point_cswap(p, q, bit);
	}
}

static void point_scalarbase(gf p[4], const u8 scalar[32])
{
	gf q[4];

	set25519(q[0], ed_x);
	set25519(q[1], ed_y);
	set25519(q[2], gf1);
	field_mul(q[3], ed_x, ed_y);
	point_scalarmult(p, q, scalar);
}

static int point_unpack_negative(gf r[4], const u8 encoded[32])
{
	gf check;
	gf den;
	gf den2;
	gf den4;
	gf den6;
	gf num;
	gf t;

	set25519(r[2], gf1);
	unpack25519(r[1], encoded);
	field_square(num, r[1]);
	field_mul(den, num, ed_d);
	field_sub(num, num, r[2]);
	field_add(den, r[2], den);
	field_square(den2, den);
	field_square(den4, den2);
	field_mul(den6, den4, den2);
	field_mul(t, den6, num);
	field_mul(t, t, den);
	pow2523(t, t);
	field_mul(t, t, num);
	field_mul(t, t, den);
	field_mul(t, t, den);
	field_mul(r[0], t, den);
	field_square(check, r[0]);
	field_mul(check, check, den);
	if (neq25519(check, num))
		field_mul(r[0], r[0], sqrt_m1);
	field_square(check, r[0]);
	field_mul(check, check, den);
	if (neq25519(check, num))
		return -EINVAL;
	if (par25519(r[0]) == (encoded[31] >> 7))
		field_sub(r[0], gf0, r[0]);
	field_mul(r[3], r[0], r[1]);
	return 0;
}

static bool point_encoding_is_canonical(const u8 encoded[32])
{
	u8 canonical[32];
	u8 y_encoding[32];
	gf y;

	memcpy(y_encoding, encoded, sizeof(y_encoding));
	y_encoding[31] &= 0x7f;
	unpack25519(y, y_encoding);
	pack25519(canonical, y);
	return !memcmp(canonical, y_encoding, sizeof(canonical));
}

static bool point_is_small_order(gf point[4])
{
	static const u8 identity[32] = { 1 };
	gf multiple[4];
	u8 encoded[32];
	unsigned int coordinate;
	unsigned int i;

	for (coordinate = 0; coordinate < 4; coordinate++)
		set25519(multiple[coordinate], point[coordinate]);
	for (i = 0; i < 3; i++)
		point_add(multiple, multiple);
	point_pack(encoded, multiple);
	return !memcmp(encoded, identity, sizeof(encoded));
}

static bool scalar_is_canonical(const u8 scalar[32])
{
	int i;

	for (i = 31; i >= 0; i--) {
		if (scalar[i] < group_order[i])
			return true;
		if (scalar[i] > group_order[i])
			return false;
	}
	return false;
}

static void scalar_mod_group_order(u8 out[32], s64 input[64])
{
	s64 carry;
	int i;
	int j;
	int k;

	for (i = 63; i >= 32; i--) {
		carry = 0;
		for (j = i - 32, k = i - 12; j < k; j++) {
			input[j] += carry - 16 * input[i] *
				group_order[j - (i - 32)];
			carry = (input[j] + 128) >> 8;
			input[j] -= carry * 256;
		}
		input[j] += carry;
		input[i] = 0;
	}
	carry = 0;
	for (j = 0; j < 32; j++) {
		input[j] += carry - (input[31] >> 4) * group_order[j];
		carry = input[j] >> 8;
		input[j] &= 0xff;
	}
	for (j = 0; j < 32; j++)
		input[j] -= carry * group_order[j];
	for (i = 0; i < 32; i++) {
		input[i + 1] += input[i] >> 8;
		out[i] = input[i] & 0xff;
	}
}

static void scalar_reduce(u8 scalar[64])
{
	s64 expanded[64];
	unsigned int i;

	for (i = 0; i < 64; i++)
		expanded[i] = scalar[i];
	memset(scalar, 0, 64);
	scalar_mod_group_order(scalar, expanded);
	memzero_explicit(expanded, sizeof(expanded));
}

static int transcript_hash(u8 digest[64], const u8 signature[64],
			   const u8 public_key[32], const void *message,
			   size_t message_len)
{
	struct crypto_shash *tfm;
	struct shash_desc *desc;
	size_t desc_size;
	int ret;

	tfm = crypto_alloc_shash("sha3-512", 0, 0);
	if (IS_ERR(tfm))
		return PTR_ERR(tfm);
	desc_size = sizeof(*desc) + crypto_shash_descsize(tfm);
	desc = kmalloc(desc_size, GFP_KERNEL);
	if (!desc) {
		crypto_free_shash(tfm);
		return -ENOMEM;
	}
	desc->tfm = tfm;
	ret = crypto_shash_init(desc);
	if (!ret)
		ret = crypto_shash_update(desc, signature, 32);
	if (!ret)
		ret = crypto_shash_update(desc, public_key, 32);
	if (!ret && message_len)
		ret = crypto_shash_update(desc, message, message_len);
	if (!ret)
		ret = crypto_shash_final(desc, digest);
	kfree_sensitive(desc);
	crypto_free_shash(tfm);
	return ret;
}

bool cvm_ed25519_verify(const u8 signature[64], const void *message,
			size_t message_len, const u8 public_key[32])
{
	gf p[4];
	gf q[4];
	u8 checker[32];
	u8 hash[64];
	bool valid = false;

	if (!signature || !public_key || (message_len && !message) ||
	    !scalar_is_canonical(signature + 32) ||
	    !point_encoding_is_canonical(signature) ||
	    !point_encoding_is_canonical(public_key) ||
	    point_unpack_negative(q, public_key) || point_is_small_order(q) ||
	    point_unpack_negative(p, signature) || point_is_small_order(p) ||
	    transcript_hash(hash, signature, public_key, message, message_len))
		goto out;

	scalar_reduce(hash);
	point_scalarmult(p, q, hash);
	point_scalarbase(q, signature + 32);
	point_add(p, q);
	point_pack(checker, p);
	valid = !memcmp(checker, signature, sizeof(checker));

out:
	memzero_explicit(hash, sizeof(hash));
	memzero_explicit(checker, sizeof(checker));
	return valid;
}
