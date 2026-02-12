# Prompt: Remove interactive command truncation caused by fixed-size input buffer

## Deficiency
Interactive mode reads input with `fgets` into a fixed 1024-byte buffer. Long commands are silently split across iterations, which can:
- break quoting and heredoc parsing,
- execute partial commands,
- produce confusing syntax errors.

## Scope
- `bin/sh/sh.c`
- Optional helper additions in `bin/sh/util.[ch]`
- Regression tests (if test harness is available)

## Required outcomes
1. Interactive mode reads complete logical lines without arbitrary 1024-byte truncation.
2. Line continuation (`\\\n`) and quoting behavior remains correct.
3. EOF and allocation failures are handled cleanly.
4. Existing non-interactive/script behavior is preserved.

## Implementation suggestions
- Prefer `getline` where available in host builds.
- If portability requires, add a growable line reader helper.

## Validation checklist
- `make -C bin/sh NATIVE_BUILD=1`
- Manual check with a command line > 4KB containing quotes/expansions.
- Verify prompt loop still updates command counter and traps correctly.

## Notes
Do not regress TTY prompt behavior while fixing input collection.
