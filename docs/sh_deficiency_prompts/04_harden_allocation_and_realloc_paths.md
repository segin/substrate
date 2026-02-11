# Prompt: Harden allocation/reallocation error handling across shell core

## Deficiency
There are many unchecked `malloc`/`calloc`/`realloc`/`strdup` calls in lexer/parser/expander/executor/job code. Several `realloc` sites overwrite the original pointer directly, which can leak memory and/or cause null dereferences on allocation failure.

## Scope
- `bin/sh/lexer.c`
- `bin/sh/parser.c`
- `bin/sh/expand.c`
- `bin/sh/exec.c`
- `bin/sh/shell_var.c`
- `bin/sh/job.c`
- `bin/sh/util.[ch]` (recommended for safe wrappers)

## Required outcomes
1. No direct `ptr = realloc(ptr, ...)` in critical paths without temporary pointer handling.
2. All allocation failures produce deterministic shell error behavior (exit status + message where appropriate).
3. No memory leaks on early-error cleanup paths introduced/left behind.
4. Parsing and execution return graceful errors rather than UB/crashes under OOM simulation.

## Constraints
- Keep behavior-compatible where possible; focus on robustness.
- Avoid introducing heavy dependencies.

## Validation checklist
- `make -C bin/sh NATIVE_BUILD=1`
- Run under ASan/UBSan if available.
- Add targeted fault-injection style tests (or wrapper hooks) for allocation failure paths in parser/expand.

## Notes
A small shared helper API for safe growth (arrays/strings) is preferred over ad-hoc repeated checks.
