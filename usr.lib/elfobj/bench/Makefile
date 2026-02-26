include ../../../Makefile.inc

CFLAGS := $(CFLAGS) -I../../../include -I../../../sys -I../src

.PHONY: all clean

all: bench_elf
	./bench_elf "$(ELFOBJ_BENCH_IMAGE)"

bench_elf: bench_elf.c ../libelfobj.a
	$(CC) $(CFLAGS) bench_elf.c ../libelfobj.a -o bench_elf

clean:
	rm -f bench_elf tmp_bench_10k.o
