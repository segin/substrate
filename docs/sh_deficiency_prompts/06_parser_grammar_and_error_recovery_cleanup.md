# Prompt: Clean up parser grammar edge cases and error recovery gaps

## Deficiency
Parser implementation shows several structural quality issues that increase maintenance risk and can mask syntax problems:
- duplicate forward declarations,
- partial/duplicated redirection parsing pathways,
- fragile error handling around unexpected tokens and here-doc parsing.

These issues can lead to inconsistent parse outcomes and make future grammar work risky.

## Scope
- `bin/sh/parser.c`
- `bin/sh/parser.h` / `bin/sh/ast.h` if needed
- Add parser regression tests

## Required outcomes
1. Remove duplicate declarations and dead parsing paths.
2. Consolidate redirection parsing logic so behavior is consistent and easier to reason about.
3. Improve syntax-error recovery boundaries (do not leave parser/lexer in inconsistent states).
4. Ensure here-doc capture is robust for delimiter handling and command boundaries.
5. Preserve currently working constructs (`if/then/fi`, `while/do/done`, `for`, `case`, subshell/grouping).

## Constraints
- Avoid massive grammar rewrites; prefer incremental refactoring with tests.
- Maintain compatibility with existing AST types unless clearly justified.

## Validation checklist
- `make -C bin/sh NATIVE_BUILD=1`
- Parser regression tests for malformed input and nested constructs.
- Confirm no new leaks in parse-failure paths.

## Notes
Document parser invariants in comments to help parallel contributors avoid regressions.
