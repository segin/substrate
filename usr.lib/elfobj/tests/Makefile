include ../../../Makefile.inc

CFLAGS := $(CFLAGS) -I../../../include -I../../../sys -I../src

TESTS = test_roundtrip test_invalid test_link test_reader_writer test_roundtrip_identity \
	test_section_segment test_symbols_hash test_reloc_backend test_link_services \
	test_debug_unwind test_validate_hardening test_perf_io test_matrix

.PHONY: all clean

all: $(TESTS)
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
	@if [ "${ELFOBJ_RUN_PERF_GATE:-0}" = "1" ]; then ./test_perf_gate.sh; fi
	./test_abi_surface.sh

%: %.c ../libelfobj.a
	$(CC) $(CFLAGS) $< ../libelfobj.a -o $@

clean:
	rm -f $(TESTS) *.o tmp_*.o tmp_*.elf tmp_identity_*.o tmp_rel*.o tmp_rela*.o \
		tmp_link_service_*.o tmp_debug_*.o tmp_debug_*.txt tmp_matrix_*.o tmp_matrix_*.elf
