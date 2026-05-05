# ex/vi Editor Audit — Deficiency Remediation

> Generated from codebase audit of `usr.lib/exvi/`.
> All issues discovered during the audit of April 2026.
> One commit per issue. Priority tiers: High → Medium → Low → Info.

## 0. Test Infrastructure

- [ ] **Test harness for exvi** (REQ: REQ-EXVI-0001)
    - [ ] Create `tests/exvi/` directory with a Makefile that builds exvi in host-native mode. (REQ: REQ-EXVI-0002)
    - [ ] Implement a scriptable test driver that feeds keystrokes/commands to exvi via PTY and captures output. (REQ: REQ-EXVI-0003)
    - [ ] Add helper functions: `assert_status_contains(text)`, `assert_buffer_modified()`, `assert_bell_rang()`, `assert_file_contents(path, expected)`. (REQ: REQ-EXVI-0004)
    - [ ] Add `make test` target to `usr.lib/exvi/Makefile` (or `tests/exvi/Makefile`). (REQ: REQ-EXVI-0005)
    - [ ] Verify the test harness builds and runs on the host. (REQ: REQ-EXVI-0006)

## 1. High Priority

### 1.1 No audible bell on colon-command errors (#1)

- [x] **Add bell to `vi_command_prompt` on error status** (REQ: REQ-EXVI-0100)
    - [x] In `usr.lib/exvi/exvi_visual.c`, `vi_command_prompt()`: after `exvi_take_pending_status()` succeeds, write `"\a"` to `STDOUT_FILENO`. (REQ: REQ-EXVI-0101)
    - [x] Verify `:q` on a modified buffer produces both a status message AND an audible bell. (REQ: REQ-EXVI-0102)
    - [x] Verify `:w` on a readonly file produces both a status message and bell. (REQ: REQ-EXVI-0103)
    - [x] Verify successful commands (`:w`, `:set number`) do NOT ring the bell. (REQ: REQ-EXVI-0104)
    - [x] Add regression test: open file, modify, send `:q\r`, assert bell and status message. (REQ: REQ-EXVI-0105)

### 1.2 `fprintf(stderr)` invisible in visual mode (#2, #6)

- [x] **Replace direct `fprintf(stderr, ...)` calls with `exvi_report_error()`** (REQ: REQ-EXVI-0200)
    - [x] `exvi_session.c:handle_pop_command()` — "No write since last change" message. (REQ: REQ-EXVI-0201)
    - [x] `exvi_session.c:handle_pop_command()` — "Tag stack empty" message. (REQ: REQ-EXVI-0202)
    - [x] `exvi_session.c:handle_tag_command()` — "Out of memory" message. (REQ: REQ-EXVI-0203)
    - [x] Audit all remaining `fprintf(stderr, ...)` calls in `exvi_cmd.c` and `exvi.c` to confirm they are only reachable in ex-mode or during startup (pre-visual). (REQ: REQ-EXVI-0204)
    - [x] Add regression test: in visual mode, Ctrl+T with modified buffer, assert status message appears. (REQ: REQ-EXVI-0205)
    - [x] Add regression test: `:pop` with empty tag stack in visual mode, assert "Tag stack empty" in status. (REQ: REQ-EXVI-0206)

## 2. Medium Priority

### 2.1 UTF-8 backspace in prompt input (#3)

- [x] **Fix single-byte backspace in `vi_prompt_input`** (REQ: REQ-EXVI-0300)
    - [x] In `usr.lib/exvi/exvi_visual.c`, `vi_prompt_input()`: replace the single-byte `buf[--len] = '\0'` backspace with a loop that walks back over UTF-8 continuation bytes (`0x80..0xBF`). (REQ: REQ-EXVI-0301)
    - [x] Extract a `vi_utf8_prev_char_offset(const char *buf, size_t len)` helper, or reuse `vi_prev_char_index` with appropriate adaptation. (REQ: REQ-EXVI-0302)
    - [x] Add regression test: type a multi-byte character in the `:` prompt, backspace, verify clean deletion. (REQ: REQ-EXVI-0303)
    - [x] Add regression test: type ASCII then multi-byte then backspace twice, verify correct result. (REQ: REQ-EXVI-0304)

### 2.2 UTF-8 word-erase (Ctrl+W) in prompt input (#7)

- [x] **Fix byte-oriented Ctrl+W in `vi_prompt_input`** (REQ: REQ-EXVI-0700)
    - [x] In `usr.lib/exvi/exvi_visual.c`, `vi_prompt_input()`, Ctrl+W handler: replace byte-by-byte `isspace`/non-space loop with UTF-8-aware character iteration. (REQ: REQ-EXVI-0701)
    - [x] Reuse the helper from REQ-EXVI-0302 for backward character stepping. (REQ: REQ-EXVI-0702)
    - [x] Add regression test: type multi-byte word, Ctrl+W, verify clean word deletion. (REQ: REQ-EXVI-0703)

## 3. Low Priority

### 3.1 `exit(0)` bypasses visual mode cleanup (#4)

- [x] **Refactor command exit to return through the visual main loop** (REQ: REQ-EXVI-0400)
    - [x] Define a sentinel return value (e.g., `EXVI_CMD_EXIT` = 2) for `handle_session_command` to signal "quit requested". (REQ: REQ-EXVI-0401)
    - [x] Modify `:q` handler to return sentinel instead of `exit(0)`. (REQ: REQ-EXVI-0402)
    - [x] Modify `:q!` handler to return sentinel instead of `exit(0)`. (REQ: REQ-EXVI-0403)
    - [x] Modify `:wq`/`:x` handler to return sentinel instead of `exit(0)`. (REQ: REQ-EXVI-0404)
    - [x] Propagate sentinel through `do_command()` → `exvi_execute_command()` → `vi_command_prompt()`. (REQ: REQ-EXVI-0405)
    - [x] In `exvi_visual_main()`, check sentinel after `vi_command_prompt()` returns; if set, `goto done`. (REQ: REQ-EXVI-0406)
    - [x] In `exvi_main()` (ex-mode loop), check sentinel and break out of loop. (REQ: REQ-EXVI-0407)
    - [x] Verify all cleanup paths (`free(vis.last_insert_text)`, `vi_clear_replace_edits`, history free, `SIGWINCH` restore) are reached. (REQ: REQ-EXVI-0408)
    - [x] Add regression test: `:wq` exits cleanly without valgrind/ASAN leak reports. (REQ: REQ-EXVI-0409)

### 3.2 ZQ warns on unsaved changes (#5)

- [x] **Add modified-buffer warning to ZQ** (REQ: REQ-EXVI-0500)
    - [x] In `exvi_visual_main()`, `ZQ` handler: if `b->modified`, display "No write since last change (use ZQ again to discard)" via `vi_set_status()` and ring bell; set a `zq_pending` flag. (REQ: REQ-EXVI-0501)
    - [x] On second consecutive ZQ (when `zq_pending` is set), exit unconditionally. (REQ: REQ-EXVI-0502)
    - [x] Clear `zq_pending` on any other keypress. (REQ: REQ-EXVI-0503)
    - [ ] Add regression test: modify buffer, ZQ once → remains open with warning; ZQ twice → exits. (REQ: REQ-EXVI-0504)
    - [ ] Add regression test: unmodified buffer, ZQ once → exits immediately. (REQ: REQ-EXVI-0505)

### 3.3 No alternate screen buffer (#8)

- [x] **Switch to alternate screen buffer on visual mode entry/exit** (REQ: REQ-EXVI-0800)
    - [x] In `vi_enable_raw()`: prepend `\x1b[?1049h` to the enter sequence. (REQ: REQ-EXVI-0801)
    - [x] In `vi_restore_terminal()`: prepend `\x1b[?1049l` to the restore sequence. (REQ: REQ-EXVI-0802)
    - [ ] Verify that shell content reappears after exiting vi. (REQ: REQ-EXVI-0803)
    - [x] Verify that `:!command` (shell escape) still works correctly (alt screen should be exited before shell, re-entered after). (REQ: REQ-EXVI-0804)
    - [ ] Add regression test: launch vi, exit, verify terminal scrollback is preserved. (REQ: REQ-EXVI-0805)

## 4. Structural / Info

### 4.1 O(n) line access performance (#9)

- [x] **Optimize `buf_get_line` for O(1) random access** (REQ: REQ-EXVI-0900)
    - [ ] Design: choose between line array with gap, skip list, or piece table. Document decision in `docs/specs/exvi.md`. (REQ: REQ-EXVI-0901)
    - [x] Implement a `line_t **line_array` in `buffer_t` that provides O(1) index access. (REQ: REQ-EXVI-0902)
    - [x] Update `buf_insert_after()` to maintain the line array (insert with memmove or gap). (REQ: REQ-EXVI-0903)
    - [x] Update `buf_delete()` to maintain the line array. (REQ: REQ-EXVI-0904)
    - [x] Update `buf_get_line()` to use the array when available. (REQ: REQ-EXVI-0905)
    - [x] Update `buf_current_line()` to use the array for O(1) line-number lookup. (REQ: REQ-EXVI-0906)
    - [ ] Benchmark: measure time for `G` command on 10K, 50K, 100K line files before and after. (REQ: REQ-EXVI-0907)
    - [ ] Add regression test: open large file, jump to last line, verify correctness. (REQ: REQ-EXVI-0908)
    - [x] Add regression test: insert/delete lines, verify `buf_get_line(N)` still returns correct content. (REQ: REQ-EXVI-0909)

## Commit Plan

Each issue gets its own commit. Suggested order:

1. Test harness (§0) — foundation for all subsequent verification
2. Bell on colon errors (§1.1) — direct fix for the reported `:q` bug
3. fprintf → exvi_report_error (§1.2) — closely related error-reporting fix
4. UTF-8 backspace in prompt (§2.1)
5. UTF-8 Ctrl+W in prompt (§2.2)
6. exit(0) cleanup refactor (§3.1)
7. ZQ modified-buffer warning (§3.2)
8. Alternate screen buffer (§3.3)
9. O(n) line access optimization (§4.1)
