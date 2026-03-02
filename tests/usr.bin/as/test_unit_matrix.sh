#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)

# 8a.1 Encode each instruction, verify bytes vs reference.
"$ROOT/tests/usr.bin/as/test_x86_legacy_encode.sh"
"$ROOT/tests/usr.bin/as/test_x86_64_encode_ext.sh"
"$ROOT/tests/usr.bin/as/test_arm_encode_core.sh"
"$ROOT/tests/usr.bin/as/test_a64_encode_core.sh"
"$ROOT/tests/usr.bin/as/test_a64_simd_core.sh"
"$ROOT/tests/usr.bin/as/test_a64_v81_core.sh"

# 8a.2 Operand range limits.
"$ROOT/tests/usr.bin/as/test_arm_thumb_core.sh"
"$ROOT/tests/usr.bin/as/test_a64_branch_core.sh"

# 8a.3 Relocation type/addend emission.
"$ROOT/tests/usr.bin/as/test_x86_reloc_core.sh"
"$ROOT/tests/usr.bin/as/test_arm_reloc_core.sh"
"$ROOT/tests/usr.bin/as/test_a64_reloc_core.sh"
"$ROOT/tests/usr.bin/as/test_relocations_elf_32_64.sh"

# 8a.4 Relaxation promotions.
"$ROOT/tests/usr.bin/as/test_relax_core.sh"

# 8a.5 Expression evaluation and symbol resolution.
"$ROOT/tests/usr.bin/as/test_parser_core.sh"
"$ROOT/tests/usr.bin/as/test_symtab_core.sh"
"$ROOT/tests/usr.bin/as/test_sections_symbols_expr_32_64.sh"

echo "ok: unit matrix"
