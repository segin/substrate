# Note on cc Test Suite

The tests in `tests/usr.bin/cc` contain many fragile tests that rely on `grep`ing specific generated assembly output. Changes in instruction selection or register allocation can cause these to fail (e.g. `native_gnu_asm_ext`, `native_gnu_asm_goto_outputs`).

The tests added for `cc_ast_to_ssa` in `test_ast2ir.c` run successfully and verify the correct behavior for missing/empty input structures.
