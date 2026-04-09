# ex/vi Shared-Core Architecture

This document records the Substrate architecture direction for `bin/ex` and `bin/vi`.
It began as the design baseline for refactoring the old monolithic `ex` and replacing the `vi` stub, and now also serves as the implementation status note for the shared `usr.lib/exvi` editor stack.

## Goals

- Keep `ex` and `vi` as first-class base utilities in `bin/`.
- Share one editor core so standalone `ex` and `vi` colon-command mode do not drift.
- Reuse `lib/edit` where it reduces terminal duplication, without forcing full-screen `vi` into a line-editor architecture.
- Prefer POSIX.1-2024 behavior first, with BSD/nvi-style behavior as the extension tie-breaker.
- Retain documented, tested extensions that improve the editor contract; do not remove supported behavior solely because it is outside the strict POSIX/BSD baseline.

## Current Repository Reality

- [`usr.lib/exvi/`](../../usr.lib/exvi/) now holds a split shared editor core:
  - [`exvi_buffer.c`](../../usr.lib/exvi/exvi_buffer.c) for buffer/file/undo primitives
  - [`exvi_parse.c`](../../usr.lib/exvi/exvi_parse.c) for address and range parsing
  - [`exvi_runtime.c`](../../usr.lib/exvi/exvi_runtime.c) for lifecycle/startup/recovery support
  - [`exvi_cmd.c`](../../usr.lib/exvi/exvi_cmd.c) and [`exvi_session.c`](../../usr.lib/exvi/exvi_session.c) for command/session behavior
  - [`exvi_visual.c`](../../usr.lib/exvi/exvi_visual.c) for the full-screen visual engine
- [`bin/ex/ex.c`](../../bin/ex/ex.c) and [`bin/vi/vi.c`](../../bin/vi/vi.c) are thin frontends over the shared core.
- [`bin/vi/vi.c`](../../bin/vi/vi.c) no longer stands alone as a stub: visual mode has a real raw-mode screen engine with motions, operators, insert/replace flow, `:` entry, search, counts, undo/redo, and PTY regression coverage.
- [`lib/edit`](../../lib/edit) already provides useful low-level primitives:
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

Build a stable shared editor core in [`usr.lib/exvi/`](../../usr.lib/exvi/) and keep [`bin/ex/ex.c`](../../bin/ex/ex.c) as a thin frontend.

Current status: largely complete. The shared `ex` core exists, is split into internal units, and is covered by host regression tests plus native shared-core tests.

Detailed remaining work is tracked in the canonical checklist below.

### Phase 2: Real `vi`

Replace [`bin/vi/vi.c`](../../bin/vi/vi.c) with a visual frontend over the same core.

Current status: active and well underway. The screen/window model, raw terminal loop, insert/replace modes, `:` entry, search, counts, many motions/operators, bounded dot-repeat, redo, scrolling, tabstop-aware rendering, and PTY integration tests are all in tree.

Detailed remaining work is tracked in the canonical checklist below.

### Phase 3: Recovery, tags, multibyte polish, and compatibility

Finish subsystem completeness:

- preserve/recover workflows
- secure and restricted modes
- startup file handling
- argument-list and tag-stack behavior
- UTF-8 and locale edge cases
- conformance matrix and man pages

Detailed remaining work is tracked in the canonical checklist below.

## Canonical Remaining Work Checklist

This is the authoritative backlog for `ex`/`vi` work that is still missing, still incomplete, or not yet tested/documented to the desired POSIX-plus-BSD standard.

Completed architecture milestones already in tree are intentionally omitted here. If an item is listed below, treat it as still open even if there is already a partial implementation.

### 1. Frontends, invocation, and startup semantics

- [x] Expand the shared option parser beyond the current `-s`, `-S`, `-v`, and `-r` baseline to the full supported `ex`/`vi` invocation contract.
- [x] Implement readonly/view-style startup behavior and option plumbing for both frontends.
- [x] Decide and implement startup command forms such as `-c`, `+cmd`, and tag-startup options if they are part of the supported contract.
- [x] Finish `ex`-to-`vi` and `vi`-to-`ex` mode handoff semantics so they match the documented frontend contract instead of only the current minimal path.
- [x] Centralize and document startup file loading order across `EXINIT`, home `.exrc`, local `.exrc`, and secure/restricted mode suppression.
- [x] Add frontend tests for interactive vs batch startup, prompt behavior, non-tty rejection, and readonly/view invocation.

### 2. Shared-core `ex` command language parity

#### 2.1 Addressing, ranges, separators, and parser fidelity

- [x] Replace the current naive `|` command splitting with delimiter-aware parsing so quoted/escaped separators do not misparse command lines.
- [x] Finish historical `"` comment behavior, including quoted delimiter edge cases and command-tail parsing.
- [x] Audit and tighten malformed address/range diagnostics so failures never mutate buffer state.
- [x] Verify per-command default addresses against historical/POSIX behavior rather than relying on the current mostly-correct defaults.
- [x] Audit `%`, `;`, mark addresses, search addresses, empty search reuse, relative offsets, and empty-command printing against BSD/vim/ex reference behavior.
- [x] Add finer-grained parser tests for malformed substitutes, malformed globals, nested separators, escaped delimiters, ambiguous abbreviations, and bad marks.

#### 2.2 Command-set completeness and command semantics

- [x] Build a command/abbreviation matrix against the supported POSIX/BSD `ex` contract, then for every command still marked "supported but absent" in the dispatcher:
  - add the dispatcher entry and accepted abbreviation spellings,
  - add direct regression coverage for the success path and the primary error path,
  - or explicitly document the command as intentionally unsupported and remove it from the supported contract.
- Supported shared `ex`/visual-`:` command contract:

| Command family | Accepted forms |
| --- | --- |
| visual handoff | `visual`, `vi` |
| version | `version`, `ver` |
| argument list | `args`, `ar`, `next`, `n`, `prev`, `rewind`, `rew` |
| recovery/session | `preserve`, `pre`, `recover`, `rec`, `pop`, `po` |
| tags/options | `tag`, `ta`, `tags`, `set`, `se` |
| file lifecycle | `quit`, `q`, `xit`, `x`, `wq`, `write`, `w`, `edit`, `e`, `read`, `r` |
| line/buffer ops | `delete`, `d`, `undo`, `u`, `put`, `pu`, `print`, `p`, `number`, `#`, `list`, `l`, `=`, `mark`, `ma`, `k{a-z}`, `file`, `f`, `append`, `a`, `insert`, `i`, `change`, `c` |
| movement/copy ops | `copy`, `co`, `t`, `move`, `m`, `join`, `j`, `yank`, `y` |
| regex/edit ops | `substitute`, `s`, `&`, `global`, `g`, `v`, `!` |

All other historical `ex` commands and aliases are currently outside the supported contract unless and until they are added here with tests.
- [x] Audit `print`, `number`, `list`, and `=` for exact current-line side effects and diagnostics.
- [x] Finish `write`, `write!`, append-write, and write-to-command semantics for all range/error/readonly cases.
- [x] Finish `read`, `read !cmd`, and insertion-point behavior for empty buffers, addressed reads, and shell-command reads.
- [x] Audit `global` and `v` against frozen-match-set semantics under destructive nested commands.
- [x] Finish the remaining `substitute` compatibility work by making the supported flag/repeat contract explicit and testing it end to end:
  - supported flags are `g`, `p`, `#`, and `l`; order is flexible, repeated supported flags are normalized away, and unsupported flags report `Bad substitute flags`,
  - `:s` with no delimiter repeats the last successful substitute on the addressed range using the saved replacement and saved `g` state,
  - empty-pattern `:s//.../` reuses the last substitute/search regex,
  - `&` repeats the last successful substitute without implicitly remembering `g`, while explicit `&g`, `&p`, `&#`, and `&l` remain supported,
  - mixed flag orders, duplicate flags, no-match cases, addressed ranges, and repeat-after-failure behavior are covered by direct regressions and oracle checks.
- [x] Complete shell escape handling and shell-capable commands under secure/restricted mode, with consistent diagnostics.
- [x] Tighten command error reporting so unknown commands, missing operands, bad destinations, and force-required paths all fail predictably.

#### 2.3 Buffer, undo, registers, and state model

- [x] Decide the supported undo/redo contract against the POSIX/BSD-first-plus-documented-extensions policy in this document, and record whether Substrate `vi` promises single-step undo, multi-step undo, redo, or a narrower deliberate subset.
  Supported contract: Substrate now keeps explicit multi-step undo history shared by `ex` and `vi`, with one saved transaction per undoable editing command or visual editing session. `u` in `vi` and `:undo` in `ex` walk backward through that history one transaction at a time. Visual-mode `Ctrl-R` walks forward through the corresponding redo stack one transaction at a time as a documented extension; standalone `ex` still has no separate redo command in contract. This keeps the POSIX `u` baseline while allowing deeper history and vi-style redo without promising a Vim-style undo tree.
- [x] If multi-step undo/redo remains in contract, replace the current single-snapshot model with explicit undo and redo transaction stacks shared by `ex` and `vi`.
- [x] Add direct unit/frontend tests for undo and redo across insert sessions, replace sessions, open-line commands, deletes/changes/yanks/puts, joins, and ex-driven edits.
- [x] Audit exact trailing-newline, empty-buffer, and empty-file semantics across load, edit, write, append, preserve, and recover.
- [x] Complete register behavior so linewise vs charwise register typing is consistent across `ex` and `vi`.
- [x] Expand `:set` from `number`, `list`, and `tabstop` to the supported POSIX/BSD-first option matrix.
  Supported shared option matrix: `number`, `list`, `autoindent`, `ignorecase`, `readonly`, `showmode`, `wrapscan`, `tabstop`, `scroll`, and `tags`, with both long names and the accepted short aliases/query forms wired through the shared `ex`/`vi` option parser.
- [x] Implement readonly/view option state, forced-write interactions, and option-driven command restrictions.
- [x] Implement search/substitute-related options once their supported policy is chosen, such as `ignorecase`, `magic`, `wrapscan`, and related behavior knobs.
  Supported policy: `wrapscan` and `ignorecase` are implemented shared options for both `ex` and `vi`; regex syntax remains fixed to the existing extended-regex engine rather than exposing a separate runtime `magic` knob.

#### 2.4 Files, arglists, tags, recovery, and startup support

- [x] Finish alternate-file reporting and filename expansion edge cases for `%` and `#`.
- [x] Deepen `args`, `next`, `prev`, and `rewind` behavior for replacement lists, unsaved buffers, diagnostics, and startup interactions.
- [x] Extend tag behavior beyond the current `tag`/`pop` baseline to the full supported tag-stack/reporting contract.
- [x] Replace the current flat `tags` lookup path with `tags` option and search-path behavior if that is part of the supported contract.
- [x] Harden preserve/recover naming, cleanup, signal-time preservation, and recover-file lifecycle behavior.
- [x] Decide and implement the supported `ex -r` UX, including any listing/selection behavior beyond direct filename recovery.

### 3. Full-screen `vi` semantic parity

#### 3.1 Screen model, redraw, resize, and terminal behavior

- [x] Introduce explicit dirty-line and status/prompt invalidation tracking so cursor-only motions no longer force a full-screen repaint.
- [x] Route insert/delete/open/join/search updates through partial redraw helpers that touch only the changed viewport rows plus status line.
- [x] Add PTY regressions that distinguish cursor-only updates from text-changing redraws on ANSI/VT100 terminals.
- [x] Audit resize behavior across normal, insert, replace, operator-pending, `:`, `/`, and `?` prompt states.
- [x] Finish redraw semantics such as `Ctrl-L` and any remaining `z` variants or screen-positioning details.
- [x] Tighten long-line, number, list, tabstop, and status-line interactions under horizontal scrolling and narrow terminals.
- [x] Audit ANSI/VT100 fallback behavior, keypad handling, and terminal capability use on simpler terminals.
- [x] Add PTY regressions specifically for redraw, resize, narrow terminals, long files, and repeated terminal-size changes.

#### 3.2 Motion completeness

- [x] Build an explicit supported-motion matrix in this document that lists every normal-mode motion key (including `g` prefixed motions), its supported count forms, and its PTY oracle coverage.
- Supported normal-mode motion matrix:

| Family | Supported keys | Supported count forms | PTY coverage status |
| --- | --- | --- | --- |
| character / column | `h`, `l`, `0`, `^`, `$`, `|` | `[count]h`, `[count]l`, `[count]\|`; `0`, `^`, `$` are bare motions | direct PTY in [`tests/bin/vi/test_vi_pty.py`](../../tests/bin/vi/test_vi_pty.py); overlap-oracle coverage present for replay-sensitive `c0` and `c^` families in [`test_vi_oracle.py`](../../tests/bin/vi/test_vi_oracle.py) |
| vertical line | `j`, `k`, `+`, `<Enter>`, `<NL>`, `-`, `_`, `g_` | `[count]j`, `[count]k`, `[count]+`, `[count]-`, `[count]_`, `[count]g_` | direct PTY in [`test_vi_pty.py`](../../tests/bin/vi/test_vi_pty.py); overlap-oracle coverage present for `_`-driven change replay, but no standalone oracle matrix yet for every plain motion form |
| screen / file position | `G`, `gg`, `H`, `M`, `L`, `Ctrl-D`, `Ctrl-U`, `Ctrl-E`, `Ctrl-Y` | `[count]G`, `[count]gg`, `[count]H`, `[count]L`; bare `M`; repeat counts on `Ctrl-D`, `Ctrl-U`, `Ctrl-E`, `Ctrl-Y` | direct PTY in [`test_vi_pty.py`](../../tests/bin/vi/test_vi_pty.py); `Ctrl-D/U/E/Y` remain standalone/countable scrolling motions only and are not operator-pending targets; no full motion-only oracle matrix yet |
| word / bigword | `w`, `W`, `e`, `E`, `b`, `B`, `ge`, `gE` | `[count]w`, `[count]W`, `[count]e`, `[count]E`, `[count]b`, `[count]B`, `[count]ge`, `[count]gE` | direct PTY in [`test_vi_pty.py`](../../tests/bin/vi/test_vi_pty.py); overlap-oracle coverage present for replay-sensitive change families such as `cb`, `cB`, `ce`, `cE`, `cge`, and `cgE` |
| sentence / paragraph | `(`, `)`, `{`, `}` | `[count](`, `[count])`, `[count]{`, `[count]}` | direct PTY in [`test_vi_pty.py`](../../tests/bin/vi/test_vi_pty.py); overlap-oracle coverage present for `c)` and `c}` replay families, but not yet for every plain counted motion form |
| section / structure | `[[`, `]]`, `[]`, `][` | `[count][[`, `[count]]]`, `[count][]`, `[count]][` | direct PTY in [`test_vi_pty.py`](../../tests/bin/vi/test_vi_pty.py); no standalone side-by-side oracle matrix yet for all plain motion forms |
| search / search-repeat | `/pattern`, `?pattern`, `n`, `N`, `*`, `#` | bare `/pattern`, `?pattern`, `*`, `#`; `[count]n`, `[count]N` | direct PTY in [`test_vi_pty.py`](../../tests/bin/vi/test_vi_pty.py); overlap-oracle coverage present for replay-sensitive `cn`, `cN`, `c*`, and `c#` families |
| find / repeat-find | `f{char}`, `F{char}`, `t{char}`, `T{char}`, `;`, `,` | `[count]f{char}`, `[count]F{char}`, `[count]t{char}`, `[count]T{char}`, `[count];`, `[count],` | direct PTY in [`test_vi_pty.py`](../../tests/bin/vi/test_vi_pty.py); overlap-oracle coverage present for replay-sensitive `cf`, `cF`, `ct`, `cT`, `c;`, and `c,` families |
| marks / matching | `'a`, `` `a ``, `%`, `[count]%` | bare mark jumps; bare `%`; `[count]%` percent-of-file target | direct PTY in [`test_vi_pty.py`](../../tests/bin/vi/test_vi_pty.py); overlap-oracle coverage present for replay-sensitive `c'`, ``c` ``, and `c%` families |

Unsupported or intentionally out-of-scope normal-mode motion keys are not implied by omission here; they remain unsupported until they are added to this matrix with tests.
- Classic POSIX/BSD cursoring aliases that are intentionally unsupported today, and therefore outside the supported contract until added with tests, are:
  - `<Space>` as an alias for `l`
  - `<Ctrl-H>` as an alias for `h`
  - `<Ctrl-N>` as an alias for `j`
  - `<Ctrl-P>` as an alias for `k`
  - `<Ctrl-F>` page-forward motion
  - `<Ctrl-B>` page-backward motion
- [x] Finish or explicitly de-support any remaining classic motion keys that are still absent once that matrix is written, so no motion is left in an implied “maybe supported” state.
- [x] Add remaining section- and structure-oriented motions if they are part of the supported BSD/POSIX contract.
- [x] Add oracles for every supported counted motion family: line motions (`j`, `k`, `+`, `-`, `_`), screen motions (`H`, `M`, `L`, `Ctrl-D`, `Ctrl-U`, `Ctrl-E`, `Ctrl-Y`), word motions (`w`, `W`, `e`, `E`, `b`, `B`, `ge`, `gE`), sentence/paragraph motions, section motions, search motions, and `%`.
- [x] Add oracles for mixed count-plus-operator forms over those same motion families, with one success case and one edge-case/no-op case each.
- [x] Tighten search-repeat, find-repeat, mark-jump, and percent-motion edge cases across line boundaries and empty matches.
- [x] Add explicit EOF, blank-line, blank-paragraph, and short-line motion oracles for `w/W`, `b/B`, `e/E`, `ge/gE`, `(`/`)`, `{`/`}`, and screen motions.

#### 3.3 Operator completeness and region semantics

- [x] Build an operator/motion matrix for supported `d`, `c`, `y`, `>`, and `<` combinations, and add PTY coverage for every cell that is part of the supported contract.
  Remaining concrete slices for that matrix:
  - [x] Lock down direct PTY coverage for column-motions (`0`, `^`, `$`, `|`) across supported operator families, including linewise coercion cases for `>` and `<`.
  - [x] Lock down direct PTY coverage for operator-pending line-motions (`_`, `+`, `<Enter>/<NL>`, `-`, `H`, `M`, `L`, `G`, `gg`) across supported operator families.
  - [x] Explicitly document that `Ctrl-D`, `Ctrl-U`, `Ctrl-E`, and `Ctrl-Y` remain supported as standalone/countable scrolling motions only, not operator-pending targets, and add regressions showing prefixed `d/c/y/>/<` does not create a hidden edit path.
  - [x] Lock down direct PTY coverage for word and bigword motions (`w`, `W`, `e`, `E`, `b`, `B`, `ge`, `gE`) across supported operator families.
  - [x] Lock down direct PTY coverage for sentence and paragraph motions (`(`, `)`, `{`, `}`) across supported operator families, including blank-separator and EOF cases.
  - [x] Lock down direct PTY coverage for section/structure motions (`[[`, `]]`, `[]`, `][`) across supported operator families.
  - [x] Lock down direct PTY coverage for search/repeat-search motions (`/`, `?`, `n`, `N`, `*`, `#`) across supported operator families.
  - [x] Lock down direct PTY coverage for find/repeat-find motions (`f`, `F`, `t`, `T`, `;`, `,`) across supported operator families.
  - [x] Lock down direct PTY coverage for mark and match motions (`'`, `` ` ``, `%`) across supported operator families.
- [x] Finish `d/c/y` behavior for backward word motions (`b`, `B`, `ge`, `gE`) and add direct PTY oracles for both same-line and cross-line cases.
- [x] Finish `d/c/y` behavior for sentence/paragraph motions (`(`, `)`, `{`, `}`) at blank separators, EOF, and empty-line boundaries.
- [x] Finish `d/c/y` cross-line charwise spans for search, find, mark, and match motions, with direct PTY oracles for forward and backward cases.
- [x] Audit and lock down linewise-vs-charwise coercion for `d/c/y/>/<` when targets land at column zero, first nonblank, end-of-line, or blank separator lines.
- [x] Complete operator support for search-based motions, mark-based motions, repeated find motions, and any remaining vertical motions.
- [x] Finish `p`/`P` cursor placement and register-type behavior after every delete/change/yank variant.
- [x] Complete named-register selection for delete/change/yank/put instead of only the currently covered register paths.

#### 3.4 Insert mode, replace mode, and repeatability

- [x] Complete insert-mode editing conveniences that are still missing or only partially correct.
- [x] Finish replace-mode semantics across tabs, short lines, newlines, and mixed insert/replace transitions.
- [x] Add `.` replay coverage for every supported change-entry command family: direct inserts (`i/a/I/A/o/O`), replace (`R`), charwise changes (`s`, `cl`, `ch`, `c0`, `c^`, `c$`, `ce`, `cE`, `cf/F/t/T`, `c;`, `c,`), linewise changes (`cc`, `c_`, `c+`, `c-`, `cH`, `cM`, `cL`), search-driven changes (`cn`, `cN`, `c*`, `c#`, `c/`, `c?`), match/mark changes (`c%`, `c'`, ``c` ``), and sentence/paragraph changes.
- [x] Add a direct PTY oracle matrix for each supported `.` replay family against the documented POSIX/BSD-first-plus-documented-extensions contract, using sanitized `vim` only as a secondary oracle where the documented behavior overlaps.
- [x] Introduce multi-step undo and redo stacks instead of the current single-snapshot model, with explicit transaction records for insert, replace, delete/change/yank/put, open-line, join, and ex-driven edits.
- [x] Define and test undo transaction boundaries so one insert session, one replace session, one open-line command, one `.` replay, and one ex command each undo as a single unit.
  Transaction-boundary contract: one entry into insert mode or replace mode runs until the terminating `<Esc>` and undoes as a single transaction; each `o`/`O` open-line command is its own transaction; one `.` replay undoes as one transaction even when it replays a change command internally; and each submitted ex command line undoes as one transaction even if it edits multiple lines within its addressed range.
- [x] Define and test redo invalidation rules so any non-redo edit clears redo history and repeated redo replays the same transaction boundaries.
  Redo contract: once one or more undo steps have populated redo history, the next non-redo edit clears that redo stack when the new transaction first mutates the buffer, and each subsequent `<Ctrl-R>` replays exactly one saved transaction boundary at a time using the same per-session/per-command grouping described above.
- [x] Audit insert-mode cursor-key, modified-cursor-key, and terminal-escape decoding so no printable garbage leaks under older terminals.
- [x] Add PTY coverage for every insert/replace control key and repeat/undo/redo path that remains underspecified.

#### 3.5 Prompt, search, and ex-entry integration

- [x] Ensure `:` in visual mode exposes the same command set, diagnostics, and option effects as standalone `ex`.
- [x] Finish `/` and `?` prompt editing behavior, prompt cancellation behavior, and search status feedback.
- [x] Complete `/`, `?`, `n`, `N`, `*`, and `#` interactions with operators, counts, wrapscan, and option state.
- [x] Add prompt-history behavior if it is part of the supported UX for `:`, `/`, and `?`.
- [x] Add PTY regressions for failed searches, cancelled prompts, wrapped searches, and search-driven operators.

### 4. Options, modes, and policy completeness

- [x] Expand the shared option table to the real supported editor option set and document each option's ex/vi impact.
  Current supported shared option table:

| Option | Accepted forms | Default | `ex` impact | `vi` impact |
| --- | --- | --- | --- | --- |
| `number` | `number`, `nu`, `nonumber`, `nonu` | off | `print`/empty-command output can be numbered; `:set` query/reporting supported | line-number gutter on redraw |
| `list` | `list`, `li`, `nolist`, `noli` | off | `print`/empty-command output uses list formatting | visual line rendering shows list-mode escapes |
| `autoindent` | `autoindent`, `ai`, `noautoindent`, `noai` | off | shared option state/query/reporting only | `o`, `O`, split-newline, and linewise change/substitute reuse the current line's indentation |
| `ignorecase` | `ignorecase`, `ic`, `noignorecase`, `noic` | off | search addresses, `global`, `v`, and `substitute` become case-insensitive | `/`, `?`, `n`, `N`, `*`, and `#` become case-insensitive |
| `readonly` | `readonly`, `ro`, `noreadonly`, `noro` | off except `-R`/`view` | plain writes are blocked unless forced | status shows `[Readonly]`; visual `:write` obeys the same restriction |
| `showmode` | `showmode`, `smd`, `noshowmode`, `nosmd` | on | shared option state/query/reporting only | insert/replace mode banners are shown or suppressed |
| `wrapscan` | `wrapscan`, `ws`, `nowrapscan`, `nows` | on | search-address and regex commands wrap at buffer ends | visual search and repeat-search wrap at buffer ends |
| `tabstop` | `tabstop=`, `ts=` and query forms | `8` | affects list/print tab expansion | affects tab rendering, absolute-column motions, and indent helpers |
| `scroll` | `scroll=`, `sc=` and query forms | `12` | shared option state/query/reporting only | until `scroll=` is set explicitly, `Ctrl-D` and `Ctrl-U` use the current visual half-screen amount; after that they use the configured value |
| `tags` | `tags=` and query forms | `tags` | `:tag` lookup walks the configured comma-separated tag-file search path | visual `Ctrl-]` and visual `:tag` use the same search path |
- [x] Implement readonly/view mode consistently in both frontends, including status display and write restrictions.
- [x] Implement secure and restricted modes consistently in both frontends, not only for the currently covered shell-command paths.
- [x] Decide and implement the supported extension policy for GNU-compatible spellings and non-conflicting aliases.
  Supported policy: only invocation forms, command names, command abbreviations, and `:set` aliases explicitly listed in this document are accepted. GNU/Vim-style long options such as `--version` and undocumented command/option spellings such as `magic` remain intentionally unsupported and are rejected with the normal frontend/command diagnostics, while Substrate-specific additions like `version`/`ver` and readonly `view`/`rex`/`rvi` remain part of the supported contract.
- [x] Document every supported option, alias, and deliberate divergence from historical BSD/nvi/vim behavior.
  Current supported alias/divergence record:

| Area | Supported contract | Deliberate divergence / note |
| --- | --- | --- |
| frontend invocation | `-s`, `-S`, `-v`, `-r`, `-R`, `-c cmd`, `-t tag`, `+cmd`, plus `view`, `rex`, and `rvi` argv0 aliases | GNU/Vim long options such as `--version` are intentionally rejected |
| ex command aliases | only the command forms listed in the section 2.2 matrix are supported | other historical `ex`, `nvi`, or Vim command names stay unsupported until listed with tests |
| `:set` aliases | only the option names and short aliases listed in the shared option table are supported | undocumented knobs such as `magic` are intentionally unsupported |
| regex/search policy | extended-regex engine with shared `ignorecase` and `wrapscan` options | no runtime `magic` option and no separate basic-regex compatibility mode |
| version reporting | `:version` / `:ver` report `Substrate vi v0.1` | this is a Substrate-specific user-visible addition |
| readonly/restricted frontends | `view`, `rex`, and `rvi` are supported readonly/restricted entry points | this follows BSD-style naming rather than adding new GNU-like option spellings |
| visual repeat | bounded `.` repeat is currently supported for the implemented operation subset | full historical insert/change replay is still open and tracked in section 3.4 |

### 5. Tags, arglists, startup files, and recovery completeness

- [x] Finish tag-stack introspection and any remaining stack-navigation commands or key bindings.
- [x] Add visual-mode tag navigation such as `Ctrl-]` and `Ctrl-T` if they are part of the supported BSD-style contract.
- [x] Complete multi-file arglist reporting and navigation interactions from both `ex` and `vi`.
- [x] Finalize `.exrc` security policy, ownership checks, directory policy, and local-vs-home precedence.
- [x] Harden preserve/recover format, crash-time preserve behavior, and recover-file cleanup.
- [x] Add tests for interrupted sessions, recover-on-startup flows, and tag/arglist state across file switches.

### 6. Multibyte, locale, and display-width correctness

Current locale fallback policy:
- The editor still runs its character classification in the byte-oriented `C` locale, even when the host environment advertises some other locale.
- Valid UTF-8 code point boundaries are now recognized for cursor motion and single-column display-column mapping, but `LC_ALL`, `LC_CTYPE`, and `LANG` still do not enable locale-specific classification or width rules.
- Tabs still expand according to `tabstop`/`list`, valid UTF-8 code points currently map as single display columns, and invalid bytes continue to use the byte-oriented fallback display path.
- `C`, `POSIX`, and non-UTF-8 locale environments are explicitly supported through that shared byte fallback while the UTF-8-specific work below remains open.

- [x] Add host-side fixtures for UTF-8 cursor motions over multibyte code points, covering `h/l`, `0/^/$`, `f/F/t/T`, `;`, `,`, `|`, `%`, and visual column tracking.
- [x] Add host-side fixtures for word/bigword/sentence/paragraph motions over multibyte text, including mixed ASCII and non-ASCII word boundaries.
- [x] Add renderer fixtures for tabs, double-width characters, combining marks, zero-width code points, and invalid byte sequences, with explicit expected display columns.
- [x] Define the supported locale fallback policy (`C`/`POSIX` and non-UTF-8 locales), implement it in the frontend, and add host tests for that policy.
- [x] Add PTY coverage for long mixed-width UTF-8 lines, combining-mark edits, invalid byte sequences, and narrow-terminal redraw behavior under those inputs.

### 7. Testing backlog

- [x] Expand [`tests/usr.lib/exvi/test_main.c`](../../tests/usr.lib/exvi/test_main.c) well beyond the current parser/set/delete/yank basics.
- [x] Build an `ex` command/feature coverage matrix from sections 2, 4, and 5, and add at least one direct success regression plus one direct failure/diagnostic regression for every supported command family still missing from `tests/bin/ex/test_ex.sh`.
  Current `ex` coverage matrix:

| Command family | Primary direct coverage | Notes |
| --- | --- | --- |
| visual handoff | [`tests/bin/ex/test_ex_frontend.py`](../../tests/bin/ex/test_ex_frontend.py) | frontend-only; `visual`/`vi` require a terminal, so batch `test_ex.sh` intentionally does not claim the success path |
| version | [`tests/bin/ex/test_ex.sh`](../../tests/bin/ex/test_ex.sh), [`tests/bin/ex/test_ex_frontend.py`](../../tests/bin/ex/test_ex_frontend.py) | direct batch success for `:version`; CLI rejection for unsupported `--version` stays in frontend coverage |
| argument list | [`tests/bin/ex/test_ex.sh`](../../tests/bin/ex/test_ex.sh), [`tests/bin/ex/test_ex_frontend.py`](../../tests/bin/ex/test_ex_frontend.py) | `args`, `next`, `prev`, `rewind`, replacement arglists, and restricted-mode diagnostics covered |
| recovery/session | [`tests/bin/ex/test_ex.sh`](../../tests/bin/ex/test_ex.sh), [`tests/bin/ex/test_ex_frontend.py`](../../tests/bin/ex/test_ex_frontend.py) | `preserve`, direct `recover %`, `-r`, recover lifecycle, and tag-stack `pop` coverage present |
| tags/options | [`tests/bin/ex/test_ex.sh`](../../tests/bin/ex/test_ex.sh), [`tests/bin/ex/test_ex_frontend.py`](../../tests/bin/ex/test_ex_frontend.py) | `tag`, `tags`, `set`, `readonly`, startup `-t`, and option diagnostics covered |
| file lifecycle | [`tests/bin/ex/test_ex.sh`](../../tests/bin/ex/test_ex.sh), [`tests/bin/ex/test_ex_frontend.py`](../../tests/bin/ex/test_ex_frontend.py) | `quit`, `wq`, `write`, `edit`, `read`, alternate/current filename expansion, readonly/restricted paths covered |
| line/buffer ops | [`tests/bin/ex/test_ex.sh`](../../tests/bin/ex/test_ex.sh), [`tests/usr.lib/exvi/test_main.c`](../../tests/usr.lib/exvi/test_main.c) | direct `delete`, `undo`, `put`, `print`, `number`, `list`, `=`, `mark`, `file`, `append`, `insert`, `change` coverage present |
| movement/copy ops | [`tests/bin/ex/test_ex.sh`](../../tests/bin/ex/test_ex.sh) | direct `copy`, `move`, `join`, and `yank` success/error coverage present |
| regex/edit ops | [`tests/bin/ex/test_ex.sh`](../../tests/bin/ex/test_ex.sh), [`tests/bin/ex/test_ex_frontend.py`](../../tests/bin/ex/test_ex_frontend.py) | `substitute`, `&`, `global`, `v`, and shell-command restrictions covered |
- [x] Build a `vi` PTY coverage matrix from sections 3, 4, and 5, and add at least one direct PTY oracle for every supported motion, operator family, prompt path, resize path, and insert/replace control path not yet represented.
  Current `vi` PTY coverage matrix:

| Area | Supported keys / paths | Primary direct PTY coverage |
| --- | --- | --- |
| character / column motions | `h`, `l`, `0`, `^`, `$`, `|` | [`tests/bin/vi/test_vi_pty.py`](../../tests/bin/vi/test_vi_pty.py) direct cases `column-motion`, `tab-column-motion`, `home-end-motion`, `tab-column-change`, `shift-pipe`, `unshift-pipe`, plus the column-zero / first-nonblank coercion cases |
| vertical line motions | `j`, `k`, `+`, `<Enter>`, `<NL>`, `-`, `_`, `g_` | direct PTY cases `underscore-motion`, `counted g_`, `plus-motion`, `empty-enter`, `d-enter`, `y-enter`, `d-newline`, `c-newline`, `y-newline`, `c-minus`, plus the startup motion sweep that exercises plain `j`/`k` |
| screen / file motions | `gg`, `G`, `H`, `M`, `L`, `Ctrl-D`, `Ctrl-U`, `Ctrl-E`, `Ctrl-Y`, `z.`/`zz`/`zt`/`zb`/`z+`/`z^` | direct PTY cases `shift-gg`, `unshift-gg`, `shift-G`, `unshift-G`, `shift-H`, `unshift-H`, `shift-M`, `unshift-M`, `shift-L`, `unshift-L`, the counted screen-motion cases, half-page / line-scroll probes, `page-down key`, `z-position`, and `z-dot` |
| word / bigword motions | `w`, `W`, `e`, `E`, `b`, `B`, `ge`, `gE` | direct PTY cases for forward/backward word operators, EOF and blank-line motion probes, `blank-word-end-count`, backward-word cross-line change cases, and the word-motion operator matrix in [`tests/bin/vi/test_vi_pty.py`](../../tests/bin/vi/test_vi_pty.py) |
| sentence / paragraph motions | `(`, `)`, `{`, `}` | direct PTY cases `sentence-forward`, `sentence-forward-line-end`, `sentence-backward`, blank-sentence forward/backward/count/delete/change/yank probes, paragraph forward/backward status probes, and the sentence/paragraph operator matrix |
| section / structure motions | `[[`, `]]`, `[]`, `][` | direct PTY cases `counted-section-motion`, `section-end-motion`, backward section status probes, and the full section operator matrix including delete/change/yank/shift/unshift |
| search / repeat-search motions | `/`, `?`, `n`, `N`, `*`, `#` | direct PTY cases `search-column`, wrapped-search and `nowrapscan` probes, failed-search prompts, search-history recall, repeat-search operator probes, and the search operator matrix |
| find / repeat-find motions | `f`, `F`, `t`, `T`, `;`, `,` | direct PTY cases for forward/backward find and till operators, `operator-semicolon-*`, `operator-comma-change`, zero-width find-change probes, and repeat-find `.` replay coverage |
| marks / matching motions | `'`, `` ` ``, `%` | direct PTY cases for line/exact mark delete/change/yank/shift/unshift, cross-line exact-mark spans, `match-motion`, percent-goto, scanned percent spans, and percent operator coverage |
| operator families | `d`, `c`, `y`, `>`, `<`, `p`, `P`, named registers | direct PTY operator matrix coverage across the supported motion families, plus register-type / put-placement probes, named-register selection, and linewise-vs-charwise coercion cases |
| repeat / history | `.`, `u`, `<Ctrl-R>` | direct PTY cases for the full supported `.` replay family matrix, `insert-undo-redo`, `replace-undo-redo`, `open-below-undo-redo`, `open-above-undo-redo`, multi-step undo, undo-boundary probes, `redo-invalidation`, and redo transaction-boundary probes |
| prompt paths | `:`, `/`, `?`, prompt edit, prompt history, cancel, failure, wrap / nowrap, visual `:` parity | direct PTY cases `search-prompt-ctrl-u`, `search-prompt-ctrl-w`, `search-cancel`, `search-fail`, `search-history`, wrapped / `nowrapscan` search probes, colon-command parity in [`tests/bin/vi/test_vi_frontend.py`](../../tests/bin/vi/test_vi_frontend.py), and visual prompt/operator integration probes |
| resize / redraw | operator-pending resize, `:` resize, `/` resize, `?` resize, narrow terminals, long files, repeated resizes, cursor-only redraw, `Ctrl-L` | direct PTY cases `operator-resize`, `colon-prompt-resize`, `search-prompt-resize`, `backward-search-prompt-resize`, `narrow-terminal`, `narrow-status`, long-file status probes, `cursor-only redraw`, explicit `Ctrl-L` redraw, and the repeated-size-change redraw tests |
| insert-mode control paths | `<Tab>`, `Ctrl-T`, `Ctrl-D`, `Ctrl-U`, `Ctrl-Y`, `Ctrl-E`, `Ctrl-A`, `Ctrl-R`, `Ctrl-V`, arrows, Home/End, Delete, Backspace, `Ctrl-Left/Right`, `Ctrl-Backspace`, `Ctrl-Delete`, `Ctrl-O` + motion / operator / ex | direct PTY cases `insert-tab`, `insert-ctrl-t`, `insert-ctrl-d`, `insert-ctrl-u`, the `Ctrl-Y` / `Ctrl-E` variants, `insert-ctrl-a`, `insert-ctrl-r`, `insert-ctrl-v`, `insert-arrow`, `insert-app-arrow`, `insert-home-end`, `insert-app-home-end`, `insert-delete`, `insert-backspace-join`, `insert-ctrl-left`, `insert-ctrl-right`, `insert-ctrl-backspace`, `insert-ctrl-delete`, and the `insert-ctrl-o-*` cases |
| replace-mode control paths | replace backspace over tabs / short lines / newlines / overruns, `Ctrl-U`, `Ctrl-Y`, `Ctrl-E`, `Ctrl-W`, `Ctrl-R`, `Ctrl-V`, `Ctrl-T`, `Ctrl-D`, arrows, Home/End, Delete, `Ctrl-Backspace`, `Ctrl-Left/Right` | direct PTY cases `replace-backspace`, `replace-tab-backspace`, `replace-newline-backspace`, `replace-overrun-backspace`, `replace-short-backspace`, `replace-mixed-backspace`, `replace-ctrl-u`, `replace-ctrl-y`, `replace-ctrl-e`, `replace-ctrl-w`, `replace-ctrl-r`, `replace-ctrl-v`, `replace-ctrl-t`, `replace-ctrl-d`, `replace-ctrl-backspace`, `replace-ctrl-delete`, `replace-ctrl-left-right`, and `replace-home-end-delete` |

  Scope note: this matrix certifies the currently supported byte-oriented `vi` contract that is exercised by the existing PTY/frontend suites. The UTF-8 and display-width work below remains explicitly open and is not implied by this checklist item.
- [x] Add fuzzing or property-based coverage for command parsing, escape-sequence parsing, and recovery-file handling.
- [x] Add stress tests for large files, long lines, narrow terminals, repeated resizes, and deep undo/redo histories.
- [x] Keep both target and host build/test paths green while coverage expands.

### 8. Documentation and conformance backlog

- [x] Write [`usr.man/man1/ex.1`](../../usr.man/man1/ex.1).
- [x] Write [`usr.man/man1/vi.1`](../../usr.man/man1/vi.1).
- [x] Add `view(1)` and any recovery/startup man pages if those entry points or subsystems are user-visible.
  Current documentation policy: `view(1)` is user-visible and documented as the readonly `vi` alias. Separate recovery/startup man pages are not added at this stage because the supported startup-file and recovery contracts are already fully documented in `ex(1)` and `vi(1)`, and there is no distinct standalone recovery binary beyond `ex -r` / `vi -r`.
- [x] Add a POSIX/BSD/GNU/Substrate conformance matrix with explicit code/tests/docs references.
  Current matrix: [`docs/specs/exvi_conformance.md`](./exvi_conformance.md)
- [x] Keep [`ARCHITECTURE.md`](../../ARCHITECTURE.md), this file, and [`docs/tasks/09-7-userland-binaries.md`](../tasks/09-7-userland-binaries.md) synchronized as the backlog closes.

## Testing Strategy

- Unit tests for buffer primitives, parser logic, and command execution.
- Regression tests for malformed address and command handling.
- PTY-driven integration tests for visual mode, raw-mode restoration, and screen refresh behavior.
- Host-mode builds for rapid iteration plus target builds for final integration.
- Fuzz/property coverage for parser and recovery-file handling as the implementation matures.

Current in-tree coverage includes:
- [`tests/bin/ex/test_ex.sh`](../../tests/bin/ex/test_ex.sh) for shared-core `ex` regressions
- [`tests/usr.lib/exvi/test_main.c`](../../tests/usr.lib/exvi/test_main.c) for native shared-core coverage
- [`tests/bin/vi/test_vi_pty.py`](../../tests/bin/vi/test_vi_pty.py) for PTY-driven full-screen `vi` behavior

## Documentation Requirements

Implementation work in this subsystem must keep these documents aligned:

- [`ARCHITECTURE.md`](../../ARCHITECTURE.md)
- `ex(1)` and `vi(1)` under `usr.man/man1/`
- any supporting recovery/startup man pages added under `usr.man/`
- task tracking under [`docs/tasks/09-7-userland-binaries.md`](../tasks/09-7-userland-binaries.md)

## Immediate Planning Consequences

- Do not keep extending the current `vi` stub in place.
- Do not treat the current `ex.c` as the long-term architecture.
- Do not treat `lib/edit` vi mode as equivalent to full-screen `vi`.
- Do mine `lib/edit` for terminal and UTF-8 helpers.
- Do keep `bin/ex` and `bin/vi` as the visible entry points.
- Do put the shared implementation in `usr.lib/exvi`.
