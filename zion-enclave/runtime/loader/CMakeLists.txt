
set(LOADER_SOURCES elf.c elf32.c elf64.c layout.c loader.c)
add_library(rt_loader ${LOADER_SOURCES})
