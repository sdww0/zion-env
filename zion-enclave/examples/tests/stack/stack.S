#if __riscv_xlen == 64
# define STORE    sd
# define LOAD     ld
# define LOG_REGBYTES 3
#elif __riscv_xlen == 32
# define STORE    sw
# define LOAD     lw
# define LOG_REGBYTES 2
#endif

.section .text

eapp_entry:
  addi sp, sp, -64

  # store 0xdeadbeef on stack
  li a0, 0xdeadbeef
  STORE a0, (sp)

  # print "[ENCLAVE] 0x"
  li a0, '['
  call putchar
  li a0, 'E'
  call putchar
  li a0, 'N'
  call putchar
  li a0, 'C'
  call putchar
  li a0, 'L'
  call putchar
  li a0, 'A'
  call putchar
  li a0, 'V'
  call putchar
  li a0, 'E'
  call putchar
  li a0, ']'
  call putchar
  li a0, ' '
  call putchar

  # load back and print hex
  LOAD a0, (sp)
  call print_hex

return:
  li a0, 12345
  li a7, 1101
  ecall

putchar:
  li a7, 1
  ecall
  ret

# print_hex(a0): prints "0x" + 16 hex digits + "\n"
print_hex:
  addi sp, sp, -32
  STORE ra, 0(sp)
  STORE s0, 8(sp)
  mv s0, a0
  li a0, '0'
  call putchar
  li a0, 'x'
  call putchar
  li t0, 60
1:
  srl t1, s0, t0
  andi t1, t1, 0xf
  li t2, 10
  bge t1, t2, 2f
  addi a0, t1, '0'
  j 3f
2:
  addi a0, t1, 'a' - 10
3:
  call putchar
  addi t0, t0, -4
  bgez t0, 1b
  li a0, '\n'
  call putchar
  LOAD ra, 0(sp)
  LOAD s0, 8(sp)
  addi sp, sp, 32
  ret
