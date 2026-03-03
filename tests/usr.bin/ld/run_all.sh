#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
TDIR="$ROOT/tests/usr.bin/ld"
TMP=${TMPDIR:-/tmp}/ldx86-testdash-$$
REQ_FILE="$TMP/req_results.txt"
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT INT TERM

total=0
passed=0
failed=0

echo "== ld test dashboard =="
echo "root: $ROOT"
echo

echo "Building ld (NATIVE_BUILD=1)..."
make -C "$ROOT/usr.bin/ld" NATIVE_BUILD=1 -j"$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 1)" >/dev/null
echo "Build: PASS"
echo

while IFS= read -r line; do
	[ -z "$line" ] && continue
	test_name=${line%%|*}
	reqs=${line#*|}
	total=$((total + 1))
	printf "[RUN ] %s\n" "$test_name"
	if sh "$TDIR/$test_name" >"$TMP/$test_name.out" 2>&1; then
		passed=$((passed + 1))
		printf "[PASS] %s\n" "$test_name"
		result=1
	else
		failed=$((failed + 1))
		printf "[FAIL] %s\n" "$test_name"
		sed -n '1,120p' "$TMP/$test_name.out"
		result=0
	fi
	for req in $reqs; do
		printf "%s %s\n" "$req" "$result" >>"$REQ_FILE"
	done
done <<'EOF'
test_link_32_64.sh|LD-U-002 LD-U-003
test_mode_parser.sh|LD-U-010 LD-E-007
test_host_mode_dual_arch.sh|LD-U-002 LD-U-003
test_unsupported_option_policy.sh|LD-U-010 LD-W-003
test_entry_option.sh|LD-U-001 LD-U-009
test_library_search_modes.sh|LD-U-004
test_group_and_whole_archive.sh|LD-S-001 LD-U-004
test_as_needed.sh|LD-E-005
test_tooling_options.sh|LD-U-011 LD-U-010
test_warning_policies.sh|LD-W-003 LD-E-001
test_input_validation_basic.sh|LD-U-012 LD-R-001
test_reloc_symbol_parsing.sh|LD-U-005 LD-U-006
test_malformed_object_hardening.sh|LD-R-001 LD-R-003 LD-R-004
test_archive_formats.sh|LD-U-004 LD-R-002
test_archive_parser_sanitization.sh|LD-R-004
test_dso_import_only.sh|LD-U-004 LD-U-005
test_dynamic_needed_as_needed.sh|LD-E-005 LD-S-003
test_dynsym_dynstr_exports.sh|LD-S-003
test_dynamic_tag_invariants.sh|LD-S-003
test_hash_style_matrix.sh|LD-U-001 LD-S-003
test_symbol_precedence.sh|LD-U-005 LD-E-003
test_unresolved_matrix.sh|LD-E-001 LD-E-002
test_visibility_resolution.sh|LD-U-005 LD-S-003
test_symbol_version_resolution.sh|LD-U-005 LD-S-003
test_section_merge_policy.sh|LD-U-008 LD-S-004
test_comdat_discard.sh|LD-U-004 LD-S-004
test_orphan_placement.sh|LD-S-004
test_gc_sections_reachability.sh|LD-E-004
test_gc_comdat_print.sh|LD-E-004 LD-U-010
test_gc_sections_matrix.sh|LD-U-007 LD-E-004
test_icf_safe.sh|LD-O-004 LD-U-007
test_icf_all_data.sh|LD-O-004 LD-R-003
test_layout_page_congruence.sh|LD-U-009 LD-S-004
test_layout_base_defaults.sh|LD-U-001 LD-U-009
test_phdr_generation.sh|LD-U-009 LD-U-010
test_gnu_stack_flags.sh|LD-U-009 LD-W-003
test_z_text_notext.sh|LD-U-009 LD-W-003
test_defsym_undefined.sh|LD-U-005
test_unresolved_provenance.sh|LD-U-010 LD-E-001
test_symbol_resolution_diff.sh|LD-U-007 LD-E-003
test_relocation_error_context.sh|LD-E-006 LD-U-010
test_rel_addend_rules.sh|LD-U-006
test_x64_reloc_overflow_signext.sh|LD-E-006 LD-U-006
test_i386_reloc_overflow_ranges.sh|LD-E-006 LD-U-006
test_i386_reloc_suite.sh|LD-U-007 LD-U-006
test_entry_fallback_matrix.sh|LD-U-001 LD-E-001 LD-U-007
test_x64_gotpcrelx_relax.sh|LD-U-006
test_x64_tls_reloc_basic.sh|LD-U-006 LD-S-003
test_x64_tls_external_models.sh|LD-U-006 LD-S-003
test_x64_ifunc_basic.sh|LD-U-006
test_x64_got_plt_runtime_sections.sh|LD-U-006 LD-S-003
test_i386_tls_reloc_basic.sh|LD-U-006 LD-S-003
test_i386_tls_external_models.sh|LD-U-006 LD-S-003
test_i386_got_plt_runtime_sections.sh|LD-U-006 LD-S-003
EOF

echo
echo "== requirement summary =="
if [ -f "$REQ_FILE" ]; then
	awk '
	{
		req=$1;
		pass=$2;
		total[req] += 1;
		ok[req] += pass;
	}
	END {
		for (r in total) {
			printf "%s: %d/%d passing\n", r, ok[r], total[r];
		}
	}
	' "$REQ_FILE" | sort
else
	echo "no requirement data produced"
fi

echo
echo "== totals =="
echo "passed: $passed"
echo "failed: $failed"
echo "total : $total"

if [ "$failed" -ne 0 ]; then
	exit 1
fi
exit 0
