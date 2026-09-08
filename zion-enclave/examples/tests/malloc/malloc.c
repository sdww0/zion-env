//******************************************************************************
// Copyright (c) 2018, The Regents of the University of California (Regents).
// All Rights Reserved. See LICENSE for license details.
//------------------------------------------------------------------------------
#include "app/eapp_utils.h"
#include "malloc.h"

static void sbi_putchar(char c) {
  register long a0 asm("a0") = c;
  register long a7 asm("a7") = 0x01;
  asm volatile("ecall" : "+r"(a0) : "r"(a7) : "memory");
}

static void puts(const char* s) {
  while (*s) sbi_putchar(*s++);
}

static void print_num(unsigned long n) {
  char buf[20];
  int i = 0;
  if (n == 0) { sbi_putchar('0'); return; }
  while (n > 0) { buf[i++] = '0' + (n % 10); n /= 10; }
  while (i > 0) sbi_putchar(buf[--i]);
}

void EAPP_ENTRY eapp_entry(){
  int* ptr = (int*) malloc(sizeof(int));
  *ptr = 11411;

  puts("[ENCLAVE] *ptr = ");
  print_num(*ptr);
  sbi_putchar('\n');

  EAPP_RETURN(*ptr);
}
