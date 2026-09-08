.section .text

eapp_entry:
  li a0, 10000000
  mv a1, zero
loop:
  beq a0, a1, done
  addi a0, a0, -1
  j loop
done:
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
  li a0, 'd'
  call putchar
  li a0, 'o'
  call putchar
  li a0, 'n'
  call putchar
  li a0, 'e'
  call putchar
  li a0, '\n'
  call putchar
return:
  li a0, 54321
  li a7, 1101
  ecall

putchar:
  li a7, 1
  ecall
  ret
