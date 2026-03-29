# ex/vi Shared-Core Architecture

This document records the Substrate architecture direction for `bin/ex` and `bin/vi`.
It is a design baseline for refactoring the current monolithic `ex` implementation and replacing the current `vi` stub with a real visual editor.

## Goals

- Keep `ex` and `vi` as first-class base utilities in `bin/`.
- Share one editor core so standalone `ex` and `vi` colon-command mode do not drift.
- Reuse `lib/edit` where it reduces terminal duplication, without forcing full-screen `vi` into a line-editor architecture.
- Prefer POSIX.1-2024 behavior first, with BSD/nvi-style behavior as the extension tie-breaker.

## Current Repository Reality

- [`bin/ex/ex.c`](/home/segin/test/bin/ex/ex.c) is a monolithic partial line editor with ad hoc buffer, file, command, and regex handling.
- [`bin/vi/vi.c`](/home/segin/test/bin/vi/vi.c) is only a stub and does not provide full-screen editing.
- [`lib/edit`](/home/segin/test/lib/edit) already provides useful low-level primitives:
  - raw terminal mode
  - termcap and ANSI fallback handling
  - escape-sequence decoding
  - history and prompt support
  - UTF-8 and display-width helpers

The design implication is straightforward: keep the helpers, replace the editor architecture.

## Target Architecture

### 1. Frontend binaries

- `bin/ex` starts in line-oriented ex mode.
- `bin/vi` starts in full-screen visual mode.
- Frontends own invocation semantics, startup options, tty suitability checks, and mode selection.
- Frontends should stay thin and delegate editing behavior into shared code.

### 2. Shared editor core

The shared core owns:

- canonical buffer representation
- current line / current file / alternate file state
- exact line-boundary and trailing-newline tracking
- marks and registers
- undo/redo transaction boundaries
- address and range parsing
- ex command parsing and execution
- regex-backed search, substitute, and global execution
- option state
- argument-list and tag-stack state
- preserve/recover state and file naming policy

The core lives in `usr.lib/exvi/` and should not depend on full-screen rendering decisions.

### 3. Visual screen engine

The visual layer owns:

- logical window state
- cursor position and screen coordinates
- topline / left-column tracking
- dirty-region and repaint logic
- resize handling
- command-mode, insert-mode, replace-mode, and prompt entry flow

This layer may call into shared terminal helpers from `lib/edit`, but it must maintain its own screen model.

## Reuse Policy for `lib/edit`

Reuse:

- raw-mode setup and restoration
- terminal capability discovery
- ANSI/VT100 fallback behavior
- escape-sequence decoding strategies
- UTF-8 and display-width helpers
- prompt/history helpers for `:` and search prompts where practical

Do not reuse as the editor core:

- the single-line edit buffer
- the line-editor vi mode state machine
- shell-oriented history semantics as a substitute for editor registers, undo, or screen state

## Behavioral Policy

- POSIX behavior is the baseline for required `ex` and `vi` semantics.
- When POSIX is silent, BSD/nvi behavior wins over GNU-specific behavior.
- GNU-compatible spellings may be accepted only when they do not change POSIX or BSD behavior.
- Modified-buffer protection applies uniformly to `:quit`, `:edit`, file-list navigation, and tag jumps.
- `vi` `:` commands must execute through the same command engine as standalone `ex`.

## Phased Implementation

### Phase 1: Shared-core `ex`

Build a stable shared editor core and reduce [`bin/ex/ex.c`](/home/segin/test/bin/ex/ex.c) to a thin frontend.

Priority work:

- create `usr.lib/exvi/` and its shared editor library layout
- move buffer, command parsing, and file handling out of `bin/ex/ex.c`
- fix command dispatch ordering and default-address handling
- enforce modified-buffer safety on quit/edit/next/prev/rewind/tag transitions
- remove in-place visual-mode placeholders from `ex`
- add host and target tests for parser, buffer, and command behavior

### Phase 2: Real `vi`

Replace [`bin/vi/vi.c`](/home/segin/test/bin/vi/vi.c) with a visual frontend over the same core.

Priority work:

- add screen/window model
- add command, insert, replace, and `:` entry modes
- implement motions, operators, counts, undo, and dot-repeat
- integrate search and ex command entry
- add PTY-driven terminal integration tests

### Phase 3: Recovery, tags, multibyte polish, and compatibility

Finish subsystem completeness:

- preserve/recover workflows
- secure and restricted modes
- startup file handling
- argument-list and tag-stack behavior
- UTF-8 and locale edge cases
- conformance matrix and man pages

## Testing Strategy

- Unit tests for buffer primitives, parser logic, and command execution.
- Regression tests for malformed address and command handling.
- PTY-driven integration tests for visual mode, raw-mode restoration, and screen refresh behavior.
- Host-mode builds for rapid iteration plus target builds for final integration.
- Fuzz/property coverage for parser and recovery-file handling as the implementation matures.

## Documentation Requirements

Implementation work in this subsystem must keep these documents aligned:

- [`ARCHITECTURE.md`](/home/segin/test/ARCHITECTURE.md)
- `ex(1)` and `vi(1)` under `usr.man/man1/`
- any supporting recovery/startup man pages added under `usr.man/`
- task tracking under [`docs/tasks/09-7-userland-binaries.md`](/home/segin/test/docs/tasks/09-7-userland-binaries.md)

## Immediate Planning Consequences

- Do not keep extending the current `vi` stub in place.
- Do not treat the current `ex.c` as the long-term architecture.
- Do not treat `lib/edit` vi mode as equivalent to full-screen `vi`.
- Do mine `lib/edit` for terminal and UTF-8 helpers.
- Do keep `bin/ex` and `bin/vi` as the visible entry points.
- Do put the shared implementation in `usr.lib/exvi`.
