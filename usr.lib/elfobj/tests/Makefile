include ../../../Makefile.inc

CFLAGS := $(CFLAGS) -I../../../include -I../../../sys -I../src

TESTS = test_roundtrip test_invalid test_link test_reader_writer test_roundtrip_identity \
	test_section_segment test_symbols_hash test_reloc_backend test_link_services \
	test_debug_unwind test_validate_hardening test_perf_io test_matrix test_multiarch_api test_write_to_buffer test_push_section

.PHONY: all clean

all: $(TESTS)
	./test_push_section
	./test_write_to_buffer
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
	./test_build_install.sh
	./test_tool_integration.sh
	@if [ "${ELFOBJ_RUN_PERF_GATE:-0}" = "1" ]; then ./test_perf_gate.sh; fi
	./test_abi_surface.sh

test_roundtrip: test_roundtrip.c ../libelfobj.a
	$(CC) $(CFLAGS) test_roundtrip.c ../libelfobj.a -o test_roundtrip

test_invalid: test_invalid.c ../libelfobj.a
	$(CC) $(CFLAGS) test_invalid.c ../libelfobj.a -o test_invalid

test_link: test_link.c ../libelfobj.a
	$(CC) $(CFLAGS) test_link.c ../libelfobj.a -o test_link

test_reader_writer: test_reader_writer.c ../libelfobj.a
	$(CC) $(CFLAGS) test_reader_writer.c ../libelfobj.a -o test_reader_writer

test_roundtrip_identity: test_roundtrip_identity.c ../libelfobj.a
	$(CC) $(CFLAGS) test_roundtrip_identity.c ../libelfobj.a -o test_roundtrip_identity

test_section_segment: test_section_segment.c ../libelfobj.a
	$(CC) $(CFLAGS) test_section_segment.c ../libelfobj.a -o test_section_segment

test_symbols_hash: test_symbols_hash.c ../libelfobj.a
	$(CC) $(CFLAGS) test_symbols_hash.c ../libelfobj.a -o test_symbols_hash

test_reloc_backend: test_reloc_backend.c ../libelfobj.a
	$(CC) $(CFLAGS) test_reloc_backend.c ../libelfobj.a -o test_reloc_backend

test_link_services: test_link_services.c ../libelfobj.a
	$(CC) $(CFLAGS) test_link_services.c ../libelfobj.a -o test_link_services

test_debug_unwind: test_debug_unwind.c ../libelfobj.a
	$(CC) $(CFLAGS) test_debug_unwind.c ../libelfobj.a -o test_debug_unwind

test_validate_hardening: test_validate_hardening.c ../libelfobj.a
	$(CC) $(CFLAGS) test_validate_hardening.c ../libelfobj.a -o test_validate_hardening

test_perf_io: test_perf_io.c ../libelfobj.a
	$(CC) $(CFLAGS) test_perf_io.c ../libelfobj.a -o test_perf_io

test_matrix: test_matrix.c ../libelfobj.a
	$(CC) $(CFLAGS) test_matrix.c ../libelfobj.a -o test_matrix

test_multiarch_api: test_multiarch_api.c ../libelfobj.a
	$(CC) $(CFLAGS) test_multiarch_api.c ../libelfobj.a -o test_multiarch_api

test_write_to_buffer: test_write_to_buffer.c ../libelfobj.a
	$(CC) $(CFLAGS) test_write_to_buffer.c ../libelfobj.a -o test_write_to_buffer

test_push_section: test_push_section.c ../libelfobj.a
	$(CC) $(CFLAGS) test_push_section.c ../libelfobj.a -o test_push_section

clean:
	rm -f $(TESTS) *.o tmp_*.o tmp_*.elf tmp_identity_*.o tmp_rel*.o tmp_rela*.o \
		tmp_link_service_*.o tmp_debug_*.o tmp_debug_*.txt tmp_matrix_*.o tmp_matrix_*.elf \
		.perf_bench.out
	rm -rf .tool_integration
