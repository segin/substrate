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
	./test_tls_tpoff_range.sh
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
	./test_copy_alias_versioned.sh
	./test_execstack_rwx.sh
	./test_link_order.sh
	./test_local_symbols.sh
	./test_merge_sections.sh
	./test_merge_size.sh
	./test_gnu_property.sh
	./test_gnu_property_size.sh
	./test_pie_exports.sh
	./test_separate_code.sh
	./test_linker_symbols.sh
	./test_reloc_section_symbols.sh
	./test_import_types.sh
	./test_dynstr_hash.sh
	./test_dynstr_index_freed.sh
	./test_x32.sh
	./test_version_script.sh
	./test_build_id.sh
	./test_code_padding.sh
	./test_local_global_same_name.sh
	./test_script_fill.sh
	./test_script_dot_expr.sh
	./test_script_sort.sh
	./test_writable_code_section.sh
	./test_script_body_align.sh
	./test_loaded_after_debug.sh
	./test_script_assert_relayout.sh
	./test_script_phdr_empty.sh
	./test_code_fill_late.sh
	./test_script_symbol_alias.sh
	./test_script_symbol_home.sh
	./test_icf_input_sections.sh
	./test_icf_safe_address_taken.sh
	./test_eh_frame_length.sh
	./test_eh_frame_cie_length.sh
	./test_eh_frame_fde_short.sh
	./test_search_state_reset.sh
	./test_spec_status.sh
	./test_architecture_files.sh
	./test_script_man_unused.sh

clean:
