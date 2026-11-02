.PHONY: all clean run

all: run

# The scripts build the linker for the host in directories of their own.
run:
	./test_script.sh
	./test_script_absolute.sh
	./test_script_segments.sh
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
	./test_nostdlib.sh
	./test_response_file.sh
	./test_library_script.sh
	./test_pie_copy.sh
	./test_copy_alias.sh
	./test_execstack_rwx.sh
	./test_link_order.sh
	./test_local_symbols.sh
	./test_merge_sections.sh
	./test_gnu_property.sh
	./test_pie_exports.sh
	./test_separate_code.sh
	./test_linker_symbols.sh
	./test_reloc_section_symbols.sh
	./test_import_types.sh
	./test_dynstr_hash.sh
	./test_x32.sh
	./test_version_script.sh
	./test_build_id.sh
	./test_code_padding.sh
	./test_local_global_same_name.sh
	./test_script_fill.sh
	./test_script_dot_expr.sh

clean:
