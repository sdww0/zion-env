#include <elf.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "loader/elf.h"
#include "loader/layout.h"
#include "mm/vm_defs.h"

#define IMAGE_SIZE 8192U

static unsigned char image[IMAGE_SIZE] __attribute__((aligned(4096)));

#define REQUIRE(condition, label)                                           \
  do {                                                                      \
    if (!(condition)) {                                                     \
      fprintf(stderr, "[EYRIE ELF SELFTEST] FAIL: %s (line %d)\n", label, \
              __LINE__);                                                    \
      return 1;                                                             \
    }                                                                       \
  } while (0)

static Elf64_Phdr *init_fixture(elf_t *elf)
{
  Elf64_Ehdr *header;
  Elf64_Phdr *programs;

  memset(image, 0, sizeof(image));
  header = (Elf64_Ehdr *)image;
  memcpy(header->e_ident, ELFMAG, SELFMAG);
  header->e_ident[EI_CLASS] = ELFCLASS64;
  header->e_ident[EI_DATA] = ELFDATA2LSB;
  header->e_ident[EI_VERSION] = EV_CURRENT;
  header->e_type = ET_EXEC;
  header->e_machine = EM_RISCV;
  header->e_version = EV_CURRENT;
  header->e_entry = 0x1000;
  header->e_ehsize = sizeof(*header);
  header->e_phoff = sizeof(*header);
  header->e_phentsize = sizeof(Elf64_Phdr);
  header->e_phnum = 2;
  header->e_shoff = sizeof(*header) + 2 * sizeof(Elf64_Phdr);
  header->e_shentsize = sizeof(Elf64_Shdr);
  header->e_shnum = 1;
  header->e_shstrndx = 0;

  programs = (Elf64_Phdr *)(image + header->e_phoff);
  programs[0].p_type = PT_LOAD;
  programs[0].p_flags = PF_R | PF_X;
  programs[0].p_offset = 0x1000;
  programs[0].p_vaddr = 0x1000;
  programs[0].p_filesz = 0x100;
  programs[0].p_memsz = 0x800;
  programs[0].p_align = 0x1000;

  programs[1].p_type = PT_LOAD;
  programs[1].p_flags = PF_R | PF_W;
  programs[1].p_offset = 0;
  programs[1].p_vaddr = 0x3000;
  programs[1].p_filesz = 0;
  programs[1].p_memsz = 0x900;
  programs[1].p_align = 0x1000;

  if (elf_newFile(image, sizeof(image), elf) != 0)
    return NULL;
  return programs;
}

static int expect_invalid(elf_t *elf)
{
  uintptr_t program_break = 0;
  return eyrie_elf_user_layout(elf, &program_break) != 0;
}

int main(void)
{
  elf_t elf;
  Elf64_Ehdr *header;
  Elf64_Phdr *programs;
  uintptr_t program_break = 0;

  programs = init_fixture(&elf);
  REQUIRE(programs != NULL &&
              eyrie_elf_user_layout(&elf, &program_break) == 0 &&
              program_break == 0x4000,
          "valid layout derives exact page-aligned break");

  programs = init_fixture(&elf);
  programs[0].p_filesz = programs[0].p_memsz + 1;
  REQUIRE(expect_invalid(&elf), "file size above memory size rejected");

  programs = init_fixture(&elf);
  programs[0].p_offset = IMAGE_SIZE;
  REQUIRE(expect_invalid(&elf), "file range outside image rejected");

  programs = init_fixture(&elf);
  programs[0].p_vaddr = 0;
  REQUIRE(expect_invalid(&elf), "null-page load rejected");

  programs = init_fixture(&elf);
  programs[1].p_vaddr = EYRIE_BRK_REGION_END - 0x800;
  programs[1].p_memsz = 0x1000;
  programs[1].p_offset = 0x800;
  REQUIRE(expect_invalid(&elf), "user-stack collision rejected");

  programs = init_fixture(&elf);
  programs[0].p_offset = 0x1001;
  REQUIRE(expect_invalid(&elf), "page-offset mismatch rejected");

  programs = init_fixture(&elf);
  programs[0].p_align = 24;
  REQUIRE(expect_invalid(&elf), "non-power-of-two alignment rejected");

  programs = init_fixture(&elf);
  header = (Elf64_Ehdr *)image;
  header->e_entry = 0x1700;
  REQUIRE(expect_invalid(&elf), "entry outside executable file bytes rejected");

  programs = init_fixture(&elf);
  programs[1].p_vaddr = 0x1800;
  programs[1].p_offset = 0x800;
  programs[1].p_align = 0x800;
  REQUIRE(expect_invalid(&elf), "loadable page overlap rejected");

  programs = init_fixture(&elf);
  programs[0].p_type = PT_NOTE;
  programs[1].p_type = PT_NOTE;
  REQUIRE(expect_invalid(&elf), "image without loadable segments rejected");

  puts("[EYRIE ELF SELFTEST] PASS: load ranges, entry, overlap, and derived break verified");
  return 0;
}
