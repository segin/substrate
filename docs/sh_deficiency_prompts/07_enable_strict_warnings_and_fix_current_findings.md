# Prompt: Enable stricter compiler diagnostics for `bin/sh` and resolve current warnings

## Deficiency
The shell builds cleanly under default flags but emits a large set of warnings under stricter diagnostics (sign conversions, missing prototypes, narrowing conversions, and API hygiene issues). This masks real bugs and reduces long-term reliability.

## Scope
- `bin/sh/Makefile`
- `bin/sh/*.c` / headers as needed to fix warnings

## Required outcomes
1. Add an opt-in strict warning mode for host builds (e.g., `SH_STRICT=1`) with flags such as:
   - `-Wall -Wextra -Wpedantic`
   - targeted safety flags (`-Wconversion`, `-Wshadow`, etc., as feasible)
2. Resolve warnings currently produced in shell sources under that mode.
3. Keep default build behavior stable if strict mode is not enabled.
4. Avoid warning-suppression pragmas unless absolutely necessary and justified.

## Validation checklist
- `make -C bin/sh clean`
- `make -C bin/sh NATIVE_BUILD=1`
- `make -C bin/sh NATIVE_BUILD=1 SH_STRICT=1`

## Notes
Prioritize semantic fixes over cosmetic casts; if a conversion is intentional, make intent explicit and safe.
