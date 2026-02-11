# Prompt: Restore and stabilize the `bin/sh` automated test harness

## Deficiency
`bin/sh/Makefile` defines multiple test targets that depend on files under `bin/sh/tests/`, but that directory does not exist in the repository. As a result, `make -C bin/sh test NATIVE_BUILD=1` fails immediately with “No rule to make target `tests/test_lexer.c`”.

This blocks regression testing and makes it difficult to safely evolve parser/executor behavior.

## Scope
- `bin/sh/Makefile`
- Add missing test sources under `bin/sh/tests/` (or adjust targets to match the real layout if tests exist elsewhere)
- Add a minimal but meaningful regression suite for lexer/parser/expand/exec/job/prompt paths

## Required outcomes
1. `make -C bin/sh test NATIVE_BUILD=1` succeeds from a clean tree.
2. The test target runs deterministic, non-interactive tests (no TTY dependency).
3. At least one test each for:
   - tokenization/lexer edge cases
   - parser structures (pipelines, conditionals, redirections)
   - parameter expansion behavior
   - builtin behavior (`set`, `export`, `readonly`, `trap`, `command`)
4. Include negative tests for syntax errors and missing files.
5. Ensure tests are host-build friendly (do not introduce target-only libc/syscall dependencies).

## Constraints
- Do **not** modify `lib/c`, `lib/sys`, crt, or syscall wrappers for host compatibility.
- Keep test binaries out of source control if they are generated artifacts.
- Keep tests independent from global machine state as much as possible.

## Validation checklist
Run and include results for:
- `make -C bin/sh clean`
- `make -C bin/sh NATIVE_BUILD=1`
- `make -C bin/sh test NATIVE_BUILD=1`

## Notes
Treat this as foundational infra work. Other shell fixes should be able to rely on this harness afterward.
