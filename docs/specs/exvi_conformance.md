# `ex` / `vi` Conformance Matrix

This matrix records the currently supported `ex` / `vi` contract for Substrate
and maps each area to:

- the closest POSIX / BSD expectation,
- any deliberate GNU / Vim divergence,
- the in-tree implementation point,
- the current regression coverage,
- and the user-facing documentation.

Use this file together with [`exvi.md`](./exvi.md) for backlog tracking.
If a behavior is not listed here, it is not part of the supported contract yet.

## Status Legend

- `supported`: implemented and covered by current code/tests/docs
- `partial`: implemented only in a narrower subset or with known parity gaps
- `unsupported`: intentionally outside the current contract
- `extension`: Substrate-specific supported behavior

## Frontends and Invocation

| Area | POSIX / BSD baseline | GNU / Vim comparison | Substrate status | Code | Tests | Docs |
| --- | --- | --- | --- | --- | --- | --- |
| `ex` frontend | POSIX / BSD-style line editor entry point | Vim also ships `ex` compatibility | `supported` | [`usr.lib/exvi/exvi.c`](../../usr.lib/exvi/exvi.c), [`bin/ex/ex.c`](../../bin/ex/ex.c) | [`tests/bin/ex/test_ex_frontend.py`](../../tests/bin/ex/test_ex_frontend.py), [`tests/bin/ex/test_ex.sh`](../../tests/bin/ex/test_ex.sh) | [`usr.man/man1/ex.1`](../../usr.man/man1/ex.1) |
| `vi` frontend | BSD-style visual editor entry point | Vim-compatible broad visual model, narrower contract | `supported` | [`usr.lib/exvi/exvi.c`](../../usr.lib/exvi/exvi.c), [`usr.lib/exvi/exvi_visual.c`](../../usr.lib/exvi/exvi_visual.c), [`bin/vi/vi.c`](../../bin/vi/vi.c) | [`tests/bin/vi/test_vi_frontend.py`](../../tests/bin/vi/test_vi_frontend.py), [`tests/bin/vi/test_vi_pty.py`](../../tests/bin/vi/test_vi_pty.py) | [`usr.man/man1/vi.1`](../../usr.man/man1/vi.1) |
| `view` alias | traditional readonly `vi` alias | Vim also supports `view` | `supported` | [`usr.lib/exvi/exvi.c`](../../usr.lib/exvi/exvi.c) | [`tests/bin/vi/test_vi_frontend.py`](../../tests/bin/vi/test_vi_frontend.py) | [`usr.man/man1/view.1`](../../usr.man/man1/view.1) |
| `rex` / `rvi` aliases | restricted aliases in BSD lineage | not a GNU long-option feature | `supported` | [`usr.lib/exvi/exvi.c`](../../usr.lib/exvi/exvi.c), [`usr.lib/exvi/exvi_runtime.c`](../../usr.lib/exvi/exvi_runtime.c) | [`tests/bin/ex/test_ex_frontend.py`](../../tests/bin/ex/test_ex_frontend.py), [`tests/bin/vi/test_vi_frontend.py`](../../tests/bin/vi/test_vi_frontend.py) | [`usr.man/man1/ex.1`](../../usr.man/man1/ex.1), [`usr.man/man1/vi.1`](../../usr.man/man1/vi.1), [`usr.man/man1/view.1`](../../usr.man/man1/view.1) |
| `-R`, `-r`, `-s`, `-S`, `-c`, `-t`, `+cmd` | BSD / POSIX-family startup forms | GNU/Vim long options intentionally not accepted | `supported` | [`usr.lib/exvi/exvi.c`](../../usr.lib/exvi/exvi.c) | [`tests/bin/ex/test_ex_frontend.py`](../../tests/bin/ex/test_ex_frontend.py), [`tests/bin/vi/test_vi_frontend.py`](../../tests/bin/vi/test_vi_frontend.py) | [`usr.man/man1/ex.1`](../../usr.man/man1/ex.1), [`usr.man/man1/vi.1`](../../usr.man/man1/vi.1) |
| GNU-style long options like `--version` | not required by POSIX / BSD `ex` / `vi` | Vim/GNU-family tools often accept them | `unsupported` | [`usr.lib/exvi/exvi.c`](../../usr.lib/exvi/exvi.c) | [`tests/bin/vi/test_vi_frontend.py`](../../tests/bin/vi/test_vi_frontend.py) | [`docs/specs/exvi.md`](./exvi.md) |

## Shared `ex` Command Language

| Area | POSIX / BSD baseline | GNU / Vim comparison | Substrate status | Code | Tests | Docs |
| --- | --- | --- | --- | --- | --- | --- |
| Addressing, ranges, `%`, `;`, marks, search addresses | core historical `ex` model | Vim broadly compatible | `supported` | [`usr.lib/exvi/exvi_parse.c`](../../usr.lib/exvi/exvi_parse.c) | [`tests/bin/ex/test_ex.sh`](../../tests/bin/ex/test_ex.sh), [`tests/usr.lib/exvi/test_main.c`](../../tests/usr.lib/exvi/test_main.c) | [`usr.man/man1/ex.1`](../../usr.man/man1/ex.1), [`docs/specs/exvi.md`](./exvi.md) |
| Delimiter-aware `|` splitting and `"` comments | BSD / historical `ex` behavior | Vim compatible in the supported subset | `supported` | [`usr.lib/exvi/exvi_parse.c`](../../usr.lib/exvi/exvi_parse.c) | [`tests/bin/ex/test_ex.sh`](../../tests/bin/ex/test_ex.sh), [`tests/usr.lib/exvi/test_main.c`](../../tests/usr.lib/exvi/test_main.c) | [`usr.man/man1/ex.1`](../../usr.man/man1/ex.1), [`docs/specs/exvi.md`](./exvi.md) |
| Shared command matrix (`q`, `w`, `e`, `r`, `d`, `p`, `s`, `g`, `v`, `tag`, `set`, etc.) | BSD / POSIX-first subset | narrower than Vim / nvi full command set | `supported` | [`usr.lib/exvi/exvi.c`](../../usr.lib/exvi/exvi.c), [`usr.lib/exvi/exvi_cmd.c`](../../usr.lib/exvi/exvi_cmd.c), [`usr.lib/exvi/exvi_session.c`](../../usr.lib/exvi/exvi_session.c) | [`tests/bin/ex/test_ex.sh`](../../tests/bin/ex/test_ex.sh) | [`usr.man/man1/ex.1`](../../usr.man/man1/ex.1), [`docs/specs/exvi.md`](./exvi.md) |
| Historical commands outside the documented matrix | many historical variants exist | Vim / nvi expose much larger command surfaces | `unsupported` | [`usr.lib/exvi/exvi.c`](../../usr.lib/exvi/exvi.c) | negative-path coverage in [`tests/bin/ex/test_ex.sh`](../../tests/bin/ex/test_ex.sh) | [`docs/specs/exvi.md`](./exvi.md) |
| `version` / `ver` | not standard POSIX `ex` | extension beyond classic `ex` | `extension` | [`usr.lib/exvi/exvi.c`](../../usr.lib/exvi/exvi.c), [`usr.lib/exvi/exvi_runtime.c`](../../usr.lib/exvi/exvi_runtime.c) | [`tests/bin/ex/test_ex_frontend.py`](../../tests/bin/ex/test_ex_frontend.py), [`tests/bin/vi/test_vi_frontend.py`](../../tests/bin/vi/test_vi_frontend.py) | [`usr.man/man1/ex.1`](../../usr.man/man1/ex.1), [`usr.man/man1/vi.1`](../../usr.man/man1/vi.1) |
| `substitute` flags `g`, `p`, `#`, `l` and repeat forms `:s`, `&` | BSD / POSIX substitute core | narrower than Vim's richer flag set | `supported` | [`usr.lib/exvi/exvi_cmd.c`](../../usr.lib/exvi/exvi_cmd.c), [`usr.lib/exvi/exvi.c`](../../usr.lib/exvi/exvi.c) | [`tests/bin/ex/test_ex.sh`](../../tests/bin/ex/test_ex.sh), [`tests/usr.lib/exvi/test_main.c`](../../tests/usr.lib/exvi/test_main.c) | [`usr.man/man1/ex.1`](../../usr.man/man1/ex.1), [`docs/specs/exvi.md`](./exvi.md) |

## Visual `vi` Behavior

| Area | POSIX / BSD baseline | GNU / Vim comparison | Substrate status | Code | Tests | Docs |
| --- | --- | --- | --- | --- | --- | --- |
| Full-screen raw-mode visual loop | BSD `vi` visual model | Vim-compatible in supported subset | `supported` | [`usr.lib/exvi/exvi_visual.c`](../../usr.lib/exvi/exvi_visual.c) | [`tests/bin/vi/test_vi_pty.py`](../../tests/bin/vi/test_vi_pty.py) | [`usr.man/man1/vi.1`](../../usr.man/man1/vi.1) |
| Core motions (`hjkl`, line, word, sentence, paragraph, section, `%`, search, marks) | BSD `vi` baseline | many Vim-compatible motions supported, some parity gaps remain | `partial` | [`usr.lib/exvi/exvi_visual.c`](../../usr.lib/exvi/exvi_visual.c) | [`tests/bin/vi/test_vi_pty.py`](../../tests/bin/vi/test_vi_pty.py) | [`usr.man/man1/vi.1`](../../usr.man/man1/vi.1), [`docs/specs/exvi.md`](./exvi.md) |
| Delete / change / yank with supported motion families | BSD `vi` operator model | broad but not exhaustive motion/operator coverage yet | `partial` | [`usr.lib/exvi/exvi_visual.c`](../../usr.lib/exvi/exvi_visual.c), [`usr.lib/exvi/exvi_cmd.c`](../../usr.lib/exvi/exvi_cmd.c) | [`tests/bin/vi/test_vi_pty.py`](../../tests/bin/vi/test_vi_pty.py) | [`usr.man/man1/vi.1`](../../usr.man/man1/vi.1), [`docs/specs/exvi.md`](./exvi.md) |
| Insert / replace mode editing, control keys, `Ctrl-O` | BSD / vi-family editing behavior | many Vim-like conveniences supported | `partial` | [`usr.lib/exvi/exvi_visual.c`](../../usr.lib/exvi/exvi_visual.c) | [`tests/bin/vi/test_vi_pty.py`](../../tests/bin/vi/test_vi_pty.py) | [`usr.man/man1/vi.1`](../../usr.man/man1/vi.1), [`docs/specs/exvi.md`](./exvi.md) |
| Bounded `.` repeat | historical `vi` expects repeat-last-change | current implementation covers only documented subset | `partial` | [`usr.lib/exvi/exvi_visual.c`](../../usr.lib/exvi/exvi_visual.c) | [`tests/bin/vi/test_vi_pty.py`](../../tests/bin/vi/test_vi_pty.py) | [`usr.man/man1/vi.1`](../../usr.man/man1/vi.1), [`docs/specs/exvi.md`](./exvi.md) |
| Visual `:` command entry parity with standalone `ex` | BSD `vi` expectation | Vim-compatible within supported command matrix | `supported` | [`usr.lib/exvi/exvi_visual.c`](../../usr.lib/exvi/exvi_visual.c), [`usr.lib/exvi/exvi.c`](../../usr.lib/exvi/exvi.c) | [`tests/bin/vi/test_vi_frontend.py`](../../tests/bin/vi/test_vi_frontend.py), [`tests/bin/vi/test_vi_pty.py`](../../tests/bin/vi/test_vi_pty.py) | [`usr.man/man1/vi.1`](../../usr.man/man1/vi.1), [`usr.man/man1/ex.1`](../../usr.man/man1/ex.1) |

## Options, Modes, and Policy

| Area | POSIX / BSD baseline | GNU / Vim comparison | Substrate status | Code | Tests | Docs |
| --- | --- | --- | --- | --- | --- | --- |
| Shared option table: `number`, `list`, `autoindent`, `ignorecase`, `readonly`, `showmode`, `wrapscan`, `tabstop`, `scroll`, `tags` | BSD-style option vocabulary | intentionally narrower than Vim's larger option space | `supported` | [`usr.lib/exvi/exvi_session.c`](../../usr.lib/exvi/exvi_session.c), [`usr.lib/exvi/exvi_runtime.c`](../../usr.lib/exvi/exvi_runtime.c), [`usr.lib/exvi/exvi_internal.h`](../../usr.lib/exvi/exvi_internal.h) | [`tests/usr.lib/exvi/test_main.c`](../../tests/usr.lib/exvi/test_main.c), [`tests/bin/ex/test_ex.sh`](../../tests/bin/ex/test_ex.sh), [`tests/bin/vi/test_vi_frontend.py`](../../tests/bin/vi/test_vi_frontend.py), [`tests/bin/vi/test_vi_pty.py`](../../tests/bin/vi/test_vi_pty.py) | [`usr.man/man1/ex.1`](../../usr.man/man1/ex.1), [`usr.man/man1/vi.1`](../../usr.man/man1/vi.1), [`docs/specs/exvi.md`](./exvi.md) |
| `ignorecase` and `wrapscan` | vi-family option knobs | supported; `magic` intentionally absent | `supported` | [`usr.lib/exvi/exvi_cmd.c`](../../usr.lib/exvi/exvi_cmd.c), [`usr.lib/exvi/exvi_visual.c`](../../usr.lib/exvi/exvi_visual.c) | [`tests/bin/ex/test_ex.sh`](../../tests/bin/ex/test_ex.sh), [`tests/bin/vi/test_vi_frontend.py`](../../tests/bin/vi/test_vi_frontend.py) | [`usr.man/man1/ex.1`](../../usr.man/man1/ex.1), [`usr.man/man1/vi.1`](../../usr.man/man1/vi.1), [`docs/specs/exvi.md`](./exvi.md) |
| runtime `magic` option | present in some historical editors and Vim | currently not implemented | `unsupported` | [`usr.lib/exvi/exvi_session.c`](../../usr.lib/exvi/exvi_session.c) | negative-path expectations in [`tests/bin/ex/test_ex.sh`](../../tests/bin/ex/test_ex.sh) | [`docs/specs/exvi.md`](./exvi.md) |
| readonly / `view` | BSD-style readonly entry and mode | supported | `supported` | [`usr.lib/exvi/exvi.c`](../../usr.lib/exvi/exvi.c), [`usr.lib/exvi/exvi_cmd.c`](../../usr.lib/exvi/exvi_cmd.c), [`usr.lib/exvi/exvi_visual.c`](../../usr.lib/exvi/exvi_visual.c) | [`tests/bin/ex/test_ex_frontend.py`](../../tests/bin/ex/test_ex_frontend.py), [`tests/bin/vi/test_vi_frontend.py`](../../tests/bin/vi/test_vi_frontend.py) | [`usr.man/man1/ex.1`](../../usr.man/man1/ex.1), [`usr.man/man1/vi.1`](../../usr.man/man1/vi.1), [`usr.man/man1/view.1`](../../usr.man/man1/view.1) |
| secure / restricted modes | traditional restricted editor behavior | supported for documented shell/file-changing paths | `supported` | [`usr.lib/exvi/exvi_runtime.c`](../../usr.lib/exvi/exvi_runtime.c), [`usr.lib/exvi/exvi_session.c`](../../usr.lib/exvi/exvi_session.c), [`usr.lib/exvi/exvi_cmd.c`](../../usr.lib/exvi/exvi_cmd.c) | [`tests/bin/ex/test_ex_frontend.py`](../../tests/bin/ex/test_ex_frontend.py), [`tests/bin/vi/test_vi_frontend.py`](../../tests/bin/vi/test_vi_frontend.py) | [`usr.man/man1/ex.1`](../../usr.man/man1/ex.1), [`usr.man/man1/vi.1`](../../usr.man/man1/vi.1), [`docs/specs/exvi.md`](./exvi.md) |

## Tags, Recovery, and Startup Files

| Area | POSIX / BSD baseline | GNU / Vim comparison | Substrate status | Code | Tests | Docs |
| --- | --- | --- | --- | --- | --- | --- |
| `tag`, `pop`, `tags`, visual `Ctrl-]`, visual `Ctrl-T` | BSD / vi-family tag stack behavior | supported in documented subset | `supported` | [`usr.lib/exvi/exvi_session.c`](../../usr.lib/exvi/exvi_session.c), [`usr.lib/exvi/exvi_visual.c`](../../usr.lib/exvi/exvi_visual.c) | [`tests/bin/ex/test_ex.sh`](../../tests/bin/ex/test_ex.sh), [`tests/bin/vi/test_vi_frontend.py`](../../tests/bin/vi/test_vi_frontend.py), [`tests/bin/vi/test_vi_pty.py`](../../tests/bin/vi/test_vi_pty.py) | [`usr.man/man1/ex.1`](../../usr.man/man1/ex.1), [`usr.man/man1/vi.1`](../../usr.man/man1/vi.1) |
| `tags=` search path option | vi-family tags-path behavior | supported as comma-separated path list | `supported` | [`usr.lib/exvi/exvi_runtime.c`](../../usr.lib/exvi/exvi_runtime.c), [`usr.lib/exvi/exvi_session.c`](../../usr.lib/exvi/exvi_session.c) | [`tests/bin/ex/test_ex.sh`](../../tests/bin/ex/test_ex.sh), [`tests/bin/vi/test_vi_frontend.py`](../../tests/bin/vi/test_vi_frontend.py) | [`usr.man/man1/ex.1`](../../usr.man/man1/ex.1), [`usr.man/man1/vi.1`](../../usr.man/man1/vi.1), [`docs/specs/exvi.md`](./exvi.md) |
| preserve / recover lifecycle | BSD-style editor recovery workflow | supported through `-r`, `.recover`, and signal-time preserve | `supported` | [`usr.lib/exvi/exvi_runtime.c`](../../usr.lib/exvi/exvi_runtime.c), [`usr.lib/exvi/exvi_buffer.c`](../../usr.lib/exvi/exvi_buffer.c) | [`tests/bin/ex/test_ex_frontend.py`](../../tests/bin/ex/test_ex_frontend.py), [`tests/bin/vi/test_vi_frontend.py`](../../tests/bin/vi/test_vi_frontend.py), [`tests/bin/ex/test_ex.sh`](../../tests/bin/ex/test_ex.sh) | [`usr.man/man1/ex.1`](../../usr.man/man1/ex.1), [`usr.man/man1/vi.1`](../../usr.man/man1/vi.1) |
| startup-file loading with `EXINIT`, home `.exrc`, local `.exrc` | BSD `ex` / `vi` startup model with safety policy | narrower and explicitly security-checked | `supported` | [`usr.lib/exvi/exvi_runtime.c`](../../usr.lib/exvi/exvi_runtime.c) | [`tests/bin/ex/test_ex.sh`](../../tests/bin/ex/test_ex.sh), [`tests/bin/ex/test_ex_frontend.py`](../../tests/bin/ex/test_ex_frontend.py) | [`usr.man/man1/ex.1`](../../usr.man/man1/ex.1), [`usr.man/man1/vi.1`](../../usr.man/man1/vi.1), [`docs/specs/exvi.md`](./exvi.md) |

## Known Open Gaps

These areas remain intentionally outside full-conformance claims today:

- full multi-change undo / redo transaction parity
- fully historical `.` repeat for insert / change / replace text replays
- complete motion/operator coverage across every classic `vi` family
- full multibyte / UTF-8 cursor and display-width correctness
- dirty-region or diff-based screen refresh instead of repaint-heavy redraw

Those gaps remain tracked in [`exvi.md`](./exvi.md).
