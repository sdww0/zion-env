	.text
	.align 2
	.globl native_crypto_fp_guard
	.type native_crypto_fp_guard, @function
native_crypto_fp_guard:
	/* Preserve the caller's ABI state, install host guard values, execute the
	 * enclave through C++, capture the values seen after return, then restore
	 * the original host FP state. */
	addi sp, sp, -64
	sd ra, 0(sp)
	fsd fs0, 8(sp)
	frcsr t0
	sd t0, 16(sp)
	sd a0, 24(sp)
	sd a3, 32(sp)
	sd a4, 40(sp)

	fmv.d.x fs0, a1
	fscsr a2
	ld a0, 24(sp)
	call native_crypto_run
	sd a0, 48(sp)

	fmv.x.d t0, fs0
	ld t1, 32(sp)
	sd t0, 0(t1)
	frcsr t0
	ld t1, 40(sp)
	sd t0, 0(t1)

	ld t0, 16(sp)
	fscsr t0
	fld fs0, 8(sp)
	ld a0, 48(sp)
	ld ra, 0(sp)
	addi sp, sp, 64
	ret
	.size native_crypto_fp_guard, .-native_crypto_fp_guard
