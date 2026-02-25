include ../../../Makefile.inc

CFLAGS := $(CFLAGS) -I../../../include -I../../../sys -I../src

TESTS = test_roundtrip test_invalid test_link test_reader_writer test_roundtrip_identity test_section_segment test_symbols_hash

.PHONY: all clean

all: $(TESTS)
	./test_roundtrip
	./test_invalid
	./test_link
	./test_reader_writer
	./test_roundtrip_identity
	./test_section_segment
	./test_symbols_hash
	./test_abi_surface.sh

%: %.c ../libelfobj.a
	$(CC) $(CFLAGS) $< ../libelfobj.a -o $@

clean:
	rm -f $(TESTS) *.o tmp_*.o tmp_*.elf tmp_identity_*.o tmp_rel*.o tmp_rela*.o
