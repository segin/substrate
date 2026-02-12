# Prompt: Correct child-process exit status and signal mapping semantics

## Deficiency
Executor wait paths frequently collapse non-normal child termination into generic status `1` and do not consistently map signaled termination to shell-compatible values (e.g., `128 + signal`). This affects external commands, subshells, and pipeline components.

## Scope
- `bin/sh/exec.c`
- Any shared status helpers you add in `bin/sh/util.[ch]`
- Tests for status edge cases

## Required outcomes
1. Waiting logic distinguishes:
   - normal exits (`WIFEXITED`),
   - signal termination (`WIFSIGNALED`),
   - stopped processes where relevant to job control.
2. `$?` reflects proper shell status semantics across simple commands, subshells, and pipelines.
3. Errors like `execve` failure return shell-appropriate status codes (e.g., command-not-found vs not-executable where applicable).
4. Behavior is consistent between interactive and non-interactive execution.

## Validation checklist
- `make -C bin/sh NATIVE_BUILD=1`
- Tests covering:
  - command exits with custom code (`exit 42`)
  - command killed by signal (`kill -TERM $$` in child context)
  - non-executable file invocation
  - missing command invocation

## Notes
Keep this task focused on status semantics; broader job-control redesign belongs elsewhere.
