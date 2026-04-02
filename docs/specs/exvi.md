# ex/vi Shared-Core Architecture

This document records the Substrate architecture direction for `bin/ex` and `bin/vi`.
It began as the design baseline for refactoring the old monolithic `ex` and replacing the `vi` stub, and now also serves as the implementation status note for the shared `usr.lib/exvi` editor stack.

## Goals

- Keep `ex` and `vi` as first-class base utilities in `bin/`.
- Share one editor core so standalone `ex` and `vi` colon-command mode do not drift.
- Reuse `lib/edit` where it reduces terminal duplication, without forcing full-screen `vi` into a line-editor architecture.
- Prefer POSIX.1-2024 behavior first, with BSD/nvi-style behavior as the extension tie-breaker.

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

- [ ] Replace the current single-snapshot undo model with a real multi-change undo/redo transaction model if historical parity requires it.
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

- [ ] Replace the current repaint-heavy renderer with a real dirty-region or diff-based refresh strategy.
- [x] Audit resize behavior across normal, insert, replace, operator-pending, `:`, `/`, and `?` prompt states.
- [x] Finish redraw semantics such as `Ctrl-L` and any remaining `z` variants or screen-positioning details.
- [x] Tighten long-line, number, list, tabstop, and status-line interactions under horizontal scrolling and narrow terminals.
- [x] Audit ANSI/VT100 fallback behavior, keypad handling, and terminal capability use on simpler terminals.
- [x] Add PTY regressions specifically for redraw, resize, narrow terminals, long files, and repeated terminal-size changes.

#### 3.2 Motion completeness

- [ ] Complete the remaining classic `vi` motions that are still absent or only partially implemented.
- [ ] Add remaining section- and structure-oriented motions if they are part of the supported BSD/POSIX contract.
- [ ] Audit every counted motion for parity with real `vi`, especially mixed count-plus-motion and count-plus-operator forms.
- [ ] Tighten search-repeat, find-repeat, mark-jump, and percent-motion edge cases across line boundaries and empty matches.
- [ ] Audit movement across empty lines, blank paragraphs, short lines, and end-of-buffer conditions against reference behavior.

#### 3.3 Operator completeness and region semantics

- [ ] Complete the remaining operator/motion combinations still missing from delete/change/yank.
- [ ] Finish cross-line charwise operator spans so they behave correctly for every motion family, not only the currently covered subsets.
- [ ] Audit linewise-vs-charwise coercion rules for every operator path, including edge cases at column zero and end-of-line.
- [x] Complete operator support for search-based motions, mark-based motions, repeated find motions, and any remaining vertical motions.
- [x] Finish `p`/`P` cursor placement and register-type behavior after every delete/change/yank variant.
- [x] Complete named-register selection for delete/change/yank/put instead of only the currently covered register paths.

#### 3.4 Insert mode, replace mode, and repeatability

- [ ] Complete insert-mode editing conveniences that are still missing or only partially correct.
- [ ] Finish replace-mode semantics across tabs, short lines, newlines, and mixed insert/replace transitions.
- [ ] Replace the current bounded `.` support with full historical repeat-last-change behavior for insert/change/replace text replays.
- [ ] Tighten undo/redo granularity so insert sessions, replace sessions, open-line commands, and repeated edits group like real `vi`.
- [x] Audit insert-mode cursor-key, modified-cursor-key, and terminal-escape decoding so no printable garbage leaks under older terminals.
- [ ] Add PTY coverage for every insert/replace control key and repeat/undo/redo path that remains underspecified.

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
| `scroll` | `scroll=`, `sc=` and query forms | `12` | shared option state/query/reporting only | `Ctrl-D` and `Ctrl-U` default to this scroll amount |
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

- [ ] Audit all cursor motions for multibyte and UTF-8 correctness.
- [ ] Audit all word/bigword/sentence/paragraph motions under multibyte text.
- [ ] Audit display-width handling for tabs, combining characters, wide characters, and invalid byte sequences.
- [ ] Add locale fallback behavior and tests for non-UTF-8 environments.
- [ ] Add PTY and host tests for long UTF-8 lines, mixed-width text, combining marks, and invalid sequences.

### 7. Testing backlog

- [ ] Expand [`tests/usr.lib/exvi/test_main.c`](../../tests/usr.lib/exvi/test_main.c) well beyond the current parser/set/delete/yank basics.
- [ ] Keep extending [`tests/bin/ex/test_ex.sh`](../../tests/bin/ex/test_ex.sh) until the remaining command-language gaps have direct regression coverage.
- [ ] Keep extending [`tests/bin/vi/test_vi_pty.py`](../../tests/bin/vi/test_vi_pty.py) until every supported motion, operator, insert-mode control, resize path, and prompt path is covered.
- [ ] Add fuzzing or property-based coverage for command parsing, escape-sequence parsing, and recovery-file handling.
- [ ] Add stress tests for large files, long lines, narrow terminals, repeated resizes, and deep undo/redo histories.
- [ ] Keep both target and host build/test paths green while coverage expands.

### 8. Documentation and conformance backlog

- [ ] Write [`usr.man/man1/ex.1`](../../usr.man/man1/ex.1).
- [ ] Write [`usr.man/man1/vi.1`](../../usr.man/man1/vi.1).
- [ ] Add `view(1)` and any recovery/startup man pages if those entry points or subsystems are user-visible.
- [ ] Add a POSIX/BSD/GNU/Substrate conformance matrix with explicit code/tests/docs references.
- [ ] Keep [`ARCHITECTURE.md`](../../ARCHITECTURE.md), this file, and [`docs/tasks/09-7-userland-binaries.md`](../tasks/09-7-userland-binaries.md) synchronized as the backlog closes.

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
