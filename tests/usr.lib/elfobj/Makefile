include ../../../Makefile.inc

CFLAGS := $(CFLAGS) -I../../../include -I../../../sys -I../../../usr.lib/elfobj/src

TESTS = test_strtab test_roundtrip test_invalid test_link test_reader_writer test_roundtrip_identity \
	test_section_segment test_symbols_hash test_reloc_backend test_link_services \
	test_debug_unwind test_validate_hardening test_perf_io test_matrix test_multiarch_api \
	test_write_to_buffer test_ensure_symbols_relocs test_push_section test_segment_align test_strtab test_layout test_push_symbol test_write_twice \
	test_write_oom test_reloc_numbers test_reloc_mutate test_lazy_link

.PHONY: all clean

all: $(TESTS)
	./test_strtab
	./test_push_symbol
	./test_push_section
	./test_segment_align
	./test_write_to_buffer
	./test_write_twice
	./test_write_oom
	./test_reloc_numbers
	./test_reloc_mutate
	./test_roundtrip
	./test_invalid
	./test_link
	./test_reader_writer
	./test_roundtrip_identity
	./test_section_segment
	./test_symbols_hash
	./test_reloc_backend
	./test_link_services
	./test_debug_unwind
	./test_validate_hardening
	./test_perf_io
	./test_matrix
	./test_multiarch_api
	./test_ensure_symbols_relocs
	./test_lazy_link
	./test_layout
	./test_build_install.sh
	./test_tool_integration.sh
	@if [ "${ELFOBJ_RUN_PERF_GATE:-0}" = "1" ]; then ./test_perf_gate.sh; fi
	./test_abi_surface.sh
	./test_dead_stores.sh

test_roundtrip: test_roundtrip.c ../../../usr.lib/elfobj/libelfobj.a
	$(CC) $(CFLAGS) test_roundtrip.c ../../../usr.lib/elfobj/libelfobj.a -o test_roundtrip

test_invalid: test_invalid.c ../../../usr.lib/elfobj/libelfobj.a
	$(CC) $(CFLAGS) test_invalid.c ../../../usr.lib/elfobj/libelfobj.a -o test_invalid

test_link: test_link.c ../../../usr.lib/elfobj/libelfobj.a
	$(CC) $(CFLAGS) test_link.c ../../../usr.lib/elfobj/libelfobj.a -o test_link

test_reader_writer: test_reader_writer.c ../../../usr.lib/elfobj/libelfobj.a
	$(CC) $(CFLAGS) test_reader_writer.c ../../../usr.lib/elfobj/libelfobj.a -o test_reader_writer

test_roundtrip_identity: test_roundtrip_identity.c ../../../usr.lib/elfobj/libelfobj.a
	$(CC) $(CFLAGS) test_roundtrip_identity.c ../../../usr.lib/elfobj/libelfobj.a -o test_roundtrip_identity

test_section_segment: test_section_segment.c ../../../usr.lib/elfobj/libelfobj.a
	$(CC) $(CFLAGS) test_section_segment.c ../../../usr.lib/elfobj/libelfobj.a -o test_section_segment

test_symbols_hash: test_symbols_hash.c ../../../usr.lib/elfobj/libelfobj.a
	$(CC) $(CFLAGS) test_symbols_hash.c ../../../usr.lib/elfobj/libelfobj.a -o test_symbols_hash

test_reloc_backend: test_reloc_backend.c ../../../usr.lib/elfobj/libelfobj.a
	$(CC) $(CFLAGS) test_reloc_backend.c ../../../usr.lib/elfobj/libelfobj.a -o test_reloc_backend

test_link_services: test_link_services.c ../../../usr.lib/elfobj/libelfobj.a
	$(CC) $(CFLAGS) test_link_services.c ../../../usr.lib/elfobj/libelfobj.a -o test_link_services

test_debug_unwind: test_debug_unwind.c ../../../usr.lib/elfobj/libelfobj.a
	$(CC) $(CFLAGS) test_debug_unwind.c ../../../usr.lib/elfobj/libelfobj.a -o test_debug_unwind

test_validate_hardening: test_validate_hardening.c ../../../usr.lib/elfobj/libelfobj.a
	$(CC) $(CFLAGS) test_validate_hardening.c ../../../usr.lib/elfobj/libelfobj.a -o test_validate_hardening

test_perf_io: test_perf_io.c ../../../usr.lib/elfobj/libelfobj.a
	$(CC) $(CFLAGS) test_perf_io.c ../../../usr.lib/elfobj/libelfobj.a -o test_perf_io

test_matrix: test_matrix.c ../../../usr.lib/elfobj/libelfobj.a
	$(CC) $(CFLAGS) test_matrix.c ../../../usr.lib/elfobj/libelfobj.a -o test_matrix

test_multiarch_api: test_multiarch_api.c ../../../usr.lib/elfobj/libelfobj.a
	$(CC) $(CFLAGS) test_multiarch_api.c ../../../usr.lib/elfobj/libelfobj.a -o test_multiarch_api

test_write_to_buffer: test_write_to_buffer.c ../../../usr.lib/elfobj/libelfobj.a
	$(CC) $(CFLAGS) test_write_to_buffer.c ../../../usr.lib/elfobj/libelfobj.a -o test_write_to_buffer

test_write_twice: test_write_twice.c ../../../usr.lib/elfobj/libelfobj.a
	$(CC) $(CFLAGS) test_write_twice.c ../../../usr.lib/elfobj/libelfobj.a -o test_write_twice

# Every allocation the library makes goes through the test's counters.
test_write_oom: test_write_oom.c ../../../usr.lib/elfobj/libelfobj.a
	$(CC) $(CFLAGS) test_write_oom.c ../../../usr.lib/elfobj/libelfobj.a -o test_write_oom \
		-Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free

# Fails to compile when two relocation constants of one machine collide.
test_reloc_numbers: test_reloc_numbers.c ../../../usr.lib/elfobj/libelfobj.a
	$(CC) $(CFLAGS) test_reloc_numbers.c ../../../usr.lib/elfobj/libelfobj.a -o test_reloc_numbers

# Relocation retargeting and clearing refuse a frozen object and mark a changed one dirty.
test_reloc_mutate: test_reloc_mutate.c ../../../usr.lib/elfobj/libelfobj.a
	$(CC) $(CFLAGS) test_reloc_mutate.c ../../../usr.lib/elfobj/libelfobj.a -o test_reloc_mutate

test_push_section: test_push_section.c ../../../usr.lib/elfobj/libelfobj.a
	$(CC) $(CFLAGS) test_push_section.c ../../../usr.lib/elfobj/libelfobj.a -o test_push_section

test_segment_align: test_segment_align.c ../../../usr.lib/elfobj/libelfobj.a
	$(CC) $(CFLAGS) test_segment_align.c ../../../usr.lib/elfobj/libelfobj.a -o test_segment_align

test_strtab: test_strtab.c ../../../usr.lib/elfobj/libelfobj.a
	$(CC) $(CFLAGS) test_strtab.c ../../../usr.lib/elfobj/libelfobj.a -o test_strtab

test_push_symbol: test_push_symbol.c ../../../usr.lib/elfobj/libelfobj.a
	$(CC) $(CFLAGS) test_push_symbol.c ../../../usr.lib/elfobj/libelfobj.a -o test_push_symbol


test_ensure_symbols_relocs: test_ensure_symbols_relocs.c ../../../usr.lib/elfobj/libelfobj.a
	$(CC) $(CFLAGS) test_ensure_symbols_relocs.c ../../../usr.lib/elfobj/libelfobj.a -o test_ensure_symbols_relocs

# Linking, resolving and section changes see a lazily parsed object's symbols and relocations.
test_lazy_link: test_lazy_link.c ../../../usr.lib/elfobj/libelfobj.a
	$(CC) $(CFLAGS) test_lazy_link.c ../../../usr.lib/elfobj/libelfobj.a -o test_lazy_link

test_layout: test_layout.c ../../../usr.lib/elfobj/libelfobj.a
	$(CC) $(CFLAGS) test_layout.c ../../../usr.lib/elfobj/libelfobj.a -o test_layout


clean:
	rm -f $(TESTS) *.o tmp_*.o tmp_*.elf tmp_identity_*.o tmp_rel*.o tmp_rela*.o \
		tmp_link_service_*.o tmp_debug_*.o tmp_debug_*.txt tmp_matrix_*.o tmp_matrix_*.elf \
		.perf_bench.out
	rm -rf .tool_integration

test_strtab: test_strtab.c ../../../usr.lib/elfobj/libelfobj.a
	$(CC) $(CFLAGS) test_strtab.c ../../../usr.lib/elfobj/libelfobj.a -o test_strtab
