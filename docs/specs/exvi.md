# ex/vi Shared-Core Architecture

This document records the Substrate architecture direction for `bin/ex` and `bin/vi`.
It began as the design baseline for refactoring the old monolithic `ex` and replacing the `vi` stub, and now also serves as the implementation status note for the shared `usr.lib/exvi` editor stack.

## Goals

- Keep `ex` and `vi` as first-class base utilities in `bin/`.
- Share one editor core so standalone `ex` and `vi` colon-command mode do not drift.
- Reuse `lib/edit` where it reduces terminal duplication, without forcing full-screen `vi` into a line-editor architecture.
- Prefer POSIX.1-2024 behavior first, with BSD/nvi-style behavior as the extension tie-breaker.

## Current Repository Reality

- [`usr.lib/exvi/`](/home/segin/test/usr.lib/exvi/) now holds a split shared editor core:
  - [`exvi_buffer.c`](/home/segin/test/usr.lib/exvi/exvi_buffer.c) for buffer/file/undo primitives
  - [`exvi_parse.c`](/home/segin/test/usr.lib/exvi/exvi_parse.c) for address and range parsing
  - [`exvi_runtime.c`](/home/segin/test/usr.lib/exvi/exvi_runtime.c) for lifecycle/startup/recovery support
  - [`exvi_cmd.c`](/home/segin/test/usr.lib/exvi/exvi_cmd.c) and [`exvi_session.c`](/home/segin/test/usr.lib/exvi/exvi_session.c) for command/session behavior
  - [`exvi_visual.c`](/home/segin/test/usr.lib/exvi/exvi_visual.c) for the full-screen visual engine
- [`bin/ex/ex.c`](/home/segin/test/bin/ex/ex.c) and [`bin/vi/vi.c`](/home/segin/test/bin/vi/vi.c) are thin frontends over the shared core.
- [`bin/vi/vi.c`](/home/segin/test/bin/vi/vi.c) no longer stands alone as a stub: visual mode has a real raw-mode screen engine with motions, operators, insert/replace flow, `:` entry, search, counts, undo/redo, and PTY regression coverage.
- [`lib/edit`](/home/segin/test/lib/edit) already provides useful low-level primitives:
  - raw terminal mode
  - termcap and ANSI fallback handling
  - escape-sequence decoding
  - history and prompt support
  - UTF-8 and display-width helpers

The design implication remains the same: keep the helpers, keep the shared-core architecture, and continue closing the remaining standards/conformance gaps rather than drifting back toward monolithic frontends.

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

Build a stable shared editor core in [`usr.lib/exvi/`](/home/segin/test/usr.lib/exvi/) and keep [`bin/ex/ex.c`](/home/segin/test/bin/ex/ex.c) as a thin frontend.

Current status: largely complete. The shared `ex` core exists, is split into internal units, and is covered by host regression tests plus native shared-core tests.

Remaining work:

- finish parity cleanup against historical/POSIX/BSD `ex` corner cases
- expand finer-grained native unit coverage beyond the existing regression harnesses
- write and ship `ex(1)` and any supporting recovery/startup documentation

### Phase 2: Real `vi`

Replace [`bin/vi/vi.c`](/home/segin/test/bin/vi/vi.c) with a visual frontend over the same core.

Current status: active and well underway. The screen/window model, raw terminal loop, insert/replace modes, `:` entry, search, counts, many motions/operators, bounded dot-repeat, redo, scrolling, tabstop-aware rendering, and PTY integration tests are all in tree.

Remaining work:

- continue filling semantic gaps against historical/POSIX/BSD `vi`
- improve live resize and screen-diff behavior where the current renderer is still simple
- expand operator/motion completeness and insert-mode editing conveniences
- write and ship `vi(1)`

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

Current in-tree coverage includes:
- [`tests/bin/ex/test_ex.sh`](/home/segin/test/tests/bin/ex/test_ex.sh) for shared-core `ex` regressions
- [`tests/usr.lib/exvi/test_main.c`](/home/segin/test/tests/usr.lib/exvi/test_main.c) for native shared-core coverage
- [`tests/bin/vi/test_vi_pty.py`](/home/segin/test/tests/bin/vi/test_vi_pty.py) for PTY-driven full-screen `vi` behavior

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
