# Prompt: Fix pipeline/job-control wait logic and exit status propagation

## Deficiency
Interactive pipeline execution has incorrect status handling:
- It waits only once on `waitpid(-pgid, ..., WUNTRACED)` and does not reap/track all processes in the pipeline.
- It returns `0` unconditionally in interactive mode, losing the actual pipeline status.

This causes inaccurate `$?`, broken short-circuit behavior after pipelines, and inconsistent job state reporting.

## Scope
- `bin/sh/exec.c`
- `bin/sh/job.c` (if needed for coherent process state updates)
- Optional small parser/executor tests if harness exists

## Required outcomes
1. Foreground interactive pipelines wait for all constituent processes (or correctly stop on job stop and preserve state).
2. The command status returned by pipeline execution reflects shell semantics (typically status of last command unless changed by explicit policy).
3. `$?` is updated correctly after interactive and non-interactive pipelines.
4. Job table state remains consistent for stopped/running/completed jobs.
5. No zombie processes are left behind.

## Constraints
- Preserve existing job-control model (pgid ownership, tcsetpgrp handoff).
- Keep changes focused; do not redesign the entire executor.

## Validation checklist
- `make -C bin/sh NATIVE_BUILD=1`
- Add/run targeted tests for:
  - `false | true`
  - `true | false`
  - stopped pipeline (`sleep ... | cat`, send SIGTSTP)
  - background pipeline status visibility (`jobs`, `fg`, `bg`)

## Notes
Be explicit in code comments about why wait strategy differs between interactive and non-interactive modes.
