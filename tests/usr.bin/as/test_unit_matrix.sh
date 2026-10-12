#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)

# 8a.1 Encode each instruction, verify bytes vs reference.
"$ROOT/tests/usr.bin/as/test_x86_legacy_encode.sh"
"$ROOT/tests/usr.bin/as/test_i386_refactor_tail.sh"
"$ROOT/tests/usr.bin/as/test_x86_64_encode_ext.sh"
"$ROOT/tests/usr.bin/as/test_i8086_corpus.sh"
"$ROOT/tests/usr.bin/as/test_i8086_realmode_core.sh"
"$ROOT/tests/usr.bin/as/test_i8086_elf_object.sh"
"$ROOT/tests/usr.bin/as/test_arm_forms.sh"

# 8a.2 Operand range limits, and 8a.3 relocation type/addend emission.
"$ROOT/tests/usr.bin/as/test_arm_targets.sh"
"$ROOT/tests/usr.bin/as/test_relocations_elf_32_64.sh"


# 8a.5 Expression evaluation and symbol resolution.
"$ROOT/tests/usr.bin/as/test_parser_core.sh"
"$ROOT/tests/usr.bin/as/test_symtab_core.sh"
"$ROOT/tests/usr.bin/as/test_sections_symbols_expr_32_64.sh"
"$ROOT/tests/usr.bin/as/test_directive_surface_32_64.sh"
"$ROOT/tests/usr.bin/as/test_numeric_local_labels.sh"
"$ROOT/tests/usr.bin/as/test_cpp_path.sh"
"$ROOT/tests/usr.bin/as/test_diagnostics_determinism.sh"
"$ROOT/tests/usr.bin/as/test_output_binary.sh"

echo "ok: unit matrix"
