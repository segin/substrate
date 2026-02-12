# `bin/sh` Deficiency Prompt Pack

This directory contains independent prompts for parallel remediation tasks found during a scan of `bin/sh/`.

## Prompts
1. `01_restore_shell_test_harness.md`
2. `02_fix_pipeline_job_control_exit_status.md`
3. `03_remove_interactive_line_length_limit.md`
4. `04_harden_allocation_and_realloc_paths.md`
5. `05_correct_child_exit_and_signal_status_semantics.md`
6. `06_parser_grammar_and_error_recovery_cleanup.md`
7. `07_enable_strict_warnings_and_fix_current_findings.md`

## Evidence snapshot used for these prompts
- `make -C bin/sh test NATIVE_BUILD=1` fails due to missing `tests/test_lexer.c`.
- Interactive mode reads with `fgets` into `char buf[1024]`.
- Interactive pipeline path returns `0` unconditionally after foreground waiting.
- Numerous host-build warnings appear under strict compiler diagnostics.

These prompts are designed to be run independently (parallel workers) with minimal overlap.
