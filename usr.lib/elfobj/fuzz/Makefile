include ../../../Makefile.inc

CFLAGS := $(CFLAGS) -I../../../include -I../../../sys -I../src

.PHONY: all clean

all: fuzz_elf_header fuzz_elf_reloc

fuzz_elf_header: fuzz_elf_header.c ../libelfobj.a
	$(CC) $(CFLAGS) fuzz_elf_header.c ../libelfobj.a -o fuzz_elf_header

fuzz_elf_reloc: fuzz_elf_reloc.c ../libelfobj.a
	$(CC) $(CFLAGS) fuzz_elf_reloc.c ../libelfobj.a -o fuzz_elf_reloc

clean:
	rm -f fuzz_elf_header fuzz_elf_reloc
