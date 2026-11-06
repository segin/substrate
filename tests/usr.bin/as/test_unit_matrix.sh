#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
D="$ROOT/tests/usr.bin/as"

# Each test is run with sh, as run-suite.sh runs them: a test need not be
# executable (test_arm_forms.sh and test_arm_targets.sh are not).

# 8a.1 Encode each instruction, verify bytes vs reference.
sh "$D/test_i386_refactor_tail.sh"
sh "$D/test_i8086_corpus.sh"
sh "$D/test_i8086_realmode_core.sh"
sh "$D/test_i8086_elf_object.sh"
sh "$D/test_arm_forms.sh"

# 8a.2 Operand range limits, and 8a.3 relocation type/addend emission.
sh "$D/test_arm_targets.sh"
sh "$D/test_relocations_elf_32_64.sh"


# 8a.5 Expression evaluation and symbol resolution.
sh "$D/test_parser_core.sh"
sh "$D/test_symtab_core.sh"
sh "$D/test_sections_symbols_expr_32_64.sh"
sh "$D/test_directive_surface_32_64.sh"
sh "$D/test_numeric_local_labels.sh"
sh "$D/test_cpp_path.sh"
sh "$D/test_diagnostics_determinism.sh"
sh "$D/test_output_binary.sh"

echo "ok: unit matrix"
