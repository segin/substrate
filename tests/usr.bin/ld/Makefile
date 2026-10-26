.PHONY: all clean run

all: run

# The scripts build the linker for the host in directories of their own.
run:
	./test_script.sh
	./test_script_absolute.sh
	./test_archive.sh
	./test_dynamic.sh
	./test_output.sh
	./test_tls.sh
	./test_got.sh
	./test_absolute_reloc.sh
	./test_narrow_reloc.sh
	./test_emit_relocs.sh
	./test_kernel_reloc.sh
	./test_page_size.sh

clean:
