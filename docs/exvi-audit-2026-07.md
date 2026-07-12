# bin/ex + bin/vi + usr.lib/exvi audit - 2026-07

Full read of the shared ex/vi editor (13,844 lines) at commit 871dcc53c:
the thin frontends `bin/ex/ex.c` (82) + `bin/vi/vi.c` (7), and the
`usr.lib/exvi/` library (exvi.c, exvi_runtime.c, exvi_parse.c,
exvi_cmd.c, exvi_buffer.c, exvi_session.c, exvi_visual.c 8625, plus
exvi_internal.h).  `bin/ex` and `bin/vi` both call `exvi_main(argc,
argv, frontend)`; the library is heavily global-state driven.

The large files were read by parallel auditors; the top findings were
then verified behaviorally against a host-built `ex`/`vi`.  Findings
numbered EXVI-NN, ranked by severity.  Status: **all open** (audit only
- no fixes applied).  `[VERIFIED]` = reproduced against the host binary;
`[code]` = confirmed by inspection (OOM / ENOSPC / signal-race paths
that are unambiguous in the source but impractical to trigger on
demand).

What's solid (verified clean): no forbidden `strcpy`/`strcat`/`sprintf`/
`vsprintf`/`gets` anywhere; all fixed status/prompt/search buffers use
bounded `snprintf`/`strlcpy` with `len+1 < size` guards; span/range
memcpy sizes are all clamped `start <= end <= len` (no size_t
underflow); UTF-8 index helpers validate against `len`; count
arithmetic saturates at 99,999,999; register indices are bounded [0,26];
`buf_delete` NULLs deleted lines out of `marks[]` so mark/operator paths
don't deref freed lines; the shell paths (`handle_shell_command`,
`exvi_popen`) correctly gate on `secure_mode||restricted_mode`, exec
directly (no `/bin/sh -c`), and drop setuid/setgid in the child;
`.exrc`/EXINIT loading checks owner + writability.  The realloc sites
that matter use the safe temp-pointer pattern (no clobber-on-failure).

## High

### EXVI-01: `:visual` from a startup command longjmps to an uninitialized jmp_buf (crash)
`handle_session_command`'s `visual`/`vi` case does
`longjmp(main_loop_jmp, EXVI_EXIT_VISUAL_HANDOFF)` (exvi.c:285), but
`main_loop_jmp` is only established by `setjmp()` at exvi.c:645 - AFTER
`load_startup_commands` runs at exvi.c:614.  So a `visual` command from
`-c`, `+cmd`, `EXINIT`, or `.exrc` jumps through a zeroed/stale jmp_buf.
**[VERIFIED]** `ex -c visual f`, `ex +visual f`, and `EXINIT=visual ex f`
all SIGSEGV (rc=139); plain `ex f` exits cleanly.  Fix: gate the
handoff longjmp on a "setjmp is live" flag, or defer startup-command
execution until after the setjmp, or set the visual-handoff exit via a
flag checked after the main loop rather than a longjmp.

### EXVI-02: file-write path ignores all I/O errors, then renames over the original (silent data loss)
`write_range_to_stream` (exvi_buffer.c:430-435) never checks
`fputs`/`fputc` returns, and `buf_write_range` never checks the
`fclose(f)` at :546 (which is where buffered data actually flushes).
On ENOSPC or any write error it still `rename(tmp, filename)` over the
original (:546-557), clears `b->modified`, and deletes the recover
snapshot - so a failed `:w` silently replaces the file with a truncated
one and destroys the means of recovery.  The append path (:490) has the
same unchecked `fclose`.  **[code]** (the temp-file *open* IS checked -
verified a permission failure preserves the target - but a mid-write /
flush failure is not).  Fix: check every `fputs`/`fputc`, check
`fclose`, and abort the rename (keeping the original + recover file) on
any error.

### EXVI-03: a hostile `tags` file runs an arbitrary ex command (`:!` -> shell)
`handle_tag_command` executes the tags file's third field as a full ex
command via `command_fn(b, tcmd)` (exvi_session.c:416), only stripping a
trailing `;"` comment.  Traditional vi/nvi deliberately restrict the
tag address to a line number or `/pattern/` because otherwise a `tags`
file in the cwd yields ex-command - and thus, via `:!`, shell -
execution.  **[VERIFIED]** a `tags` line `sym\tfile\t!echo TAGPWNED` runs
the shell command on `ex -t sym`.  Fix: accept only a numeric line
address or a `/re/`?re?` search as the tag address; reject anything
else.

### EXVI-04: NATIVE_BUILD link is broken, so the vi/ex host test harness can't build
`bin/vi/Makefile` and `bin/ex/Makefile` hardcode
`LDADD = -L.../exvi -l:libexvi.so.0` even under `NATIVE_BUILD=1`, but a
native `libexvi` build produces only `libexvi.a` (the Makefile builds
`$(SHLIB)` only when not NATIVE_BUILD).  **[VERIFIED]** `make -C bin/vi
NATIVE_BUILD=1` fails with "cannot find -l:libexvi.so.0", so
`tests/bin/vi` (pty/oracle/stress/frontend suites) and `tests/bin/ex`
cannot build or run.  Same class as the recently-fixed bc BC-20.  Fix:
select `libexvi.a` for NATIVE_BUILD.

### EXVI-05: line_array cache is rebuilt from uninitialized memory after an OOM fallback
When `buf_line_array_grow` fails, `buf_insert_after` frees `line_array`
and NULLs it "to fall back to the linked list" (exvi_buffer.c:198-214),
but the fallback is not sticky: the next insert calls
`buf_line_array_grow(b, count+1)` whose `realloc(NULL,...)` succeeds and
returns an **uninitialized** array; `buf_line_index` then scans garbage
and only `array[0]` gets set, so `buf_get_line(k>1)` returns a wild
pointer.  **[code]** (OOM-gated).  Fix: on the realloc-failure fallback,
mark the cache permanently disabled (or fully rebuild it from the list
before use), don't let a later smaller realloc resurrect a
half-populated array.

## Medium

### EXVI-06: regex address/substitute patterns lose every backslash escape
`parse_delimited_text` unconditionally drops the backslash of any `\x`
pair (`text[len++] = src[1]; src += 2;`, exvi_parse.c:275) before the
pattern reaches `regcomp`.  So `\.`, `\*`, `\[`, `\(` etc. are stripped
and the metacharacter goes through bare.  **[VERIFIED]** `s/a\.b/Z/`
matches `aXb` (the `\.` became an any-char `.`).  Fix: only unescape the
delimiter itself (and `\\`); pass other `\x` sequences through to the
regex engine intact.

### EXVI-07: substitute replacement has no `&` / `\N` backreference expansion
`apply_substitute_range` `memcpy`s the replacement verbatim
(exvi_cmd.c:151).  POSIX ex requires `&` to expand to the whole match
and `\1`..`\9` to subexpressions.  **[VERIFIED]** `s/b../[&]/` on `abcd`
yields `a[&]d` (literal `&`) instead of `a[bcd]d`.  Fix: expand `&` and
`\N` against the `regmatch_t` groups when building the replacement
(honour `\&` for a literal ampersand).

### EXVI-08: `t` (copy) with the destination inside the source range duplicates the wrong lines
`handle_copy_command` (exvi_cmd.c:503-511) walks `src->next` while
`buf_insert_after` splices the copies into the same chain, and (unlike
`handle_move_command`, which rejects dest-in-range) has no guard or
source snapshot.  **[VERIFIED]** `2,3t2` on `l1..l4` produces
`l1 l2 l2 l2 l3 l4` instead of `l1 l2 l2 l3 l3 l4`.  Fix: snapshot the
source line texts before inserting (as move-into-range would need), or
reject/handle dest within [addr1,addr2].

### EXVI-09: SIGINT handler longjmps out of the middle of malloc/buffer mutation
`handle_sigint` unconditionally `longjmp(main_loop_jmp, 1)`
(exvi_runtime.c) with handlers installed at exvi.c:618 - before the
setjmp at :645 and across the whole command loop.  A SIGINT during
`do_command`'s `malloc`/`realloc`/list splice unwinds with the heap
half-updated (corruption), and one delivered in the install-vs-setjmp
window (or on a repeat `exvi_main`) jumps through a stale jmp_buf.
**[code]**.  Fix: set a `volatile sig_atomic_t` interrupt flag in the
handler and check it at safe points; only longjmp once the main loop is
established and never from inside an allocation.

### EXVI-10: owned arglist leaks on the recover-mode error returns
`exvi_set_owned_arglist` transfers ownership of the strdup'd argv to
session state, released only by `exvi_cleanup_session_state()` at the
`out:` path (exvi.c:699).  The recover-mode error returns (exvi.c:588-593
strdup failure; 596-604 load failure) return via
`exvi_cleanup_runtime()` without the session cleanup, leaking the
arglist and its copies.  **[code]**.

### EXVI-11: `buf_copy` drops all marks and the cursor, so undo/redo loses them
Undo/redo snapshots go through `buf_copy` (exvi_buffer.c:320-345), which
allocates fresh nodes but never repopulates `marks[]`, `mark_cols[]`, or
`cur`.  After any `u`/redo, all a-z marks and the cursor position are
gone.  **[VERIFIED-ish]** a mark set then restored-through-undo is no
longer reachable ("Bad address").  Functional, not unsafe (marks are
cleared, not dangling).  Fix: re-map marks/cur to the corresponding
new lines during buf_copy (by index).

### EXVI-12: `tags_path` leaked on two error returns in `handle_tag_command`
`open_tags_file(&tags_path)` allocates `tags_path`; the "No write since
last change" and `push_tag_frame`-OOM returns (exvi_session.c:388-396)
free `line` and `fclose(f)` but never `free(tags_path)`.  **[code]**.

### EXVI-13: `last_insert_text` leaked on the `Q` (visual->ex) handoff
The visual loop's `done:` exit frees `vis.last_insert_text`
(exvi_visual.c:8605) but the `ex_mode:` handoff path (:8616-8624) does
not, so pressing `Q` after any insert/change leaks that buffer (the
stack `vis` is then gone).  **[code]**.

## Low

### EXVI-14: unchecked `strdup` before `buf_read_file` (exvi.c:608)
`buf.filename = strdup(...); buf_read_file(&buf, buf.filename);` - NULL
on OOM is handed straight to `buf_read_file` (NULL deref).  The recover
path checks; this one doesn't.

### EXVI-15: `recover_signal_save` walks the buffer list from a signal handler
`handle_sigterm` -> `recover_signal_save` (exvi_runtime.c:572-606)
dereferences `b->head`/`l->next`/`l->text` while the main flow may be
mid-mutation, risking a crash inside the handler.  `open`/`write`/`close`
are async-safe but the list walk is not.

### EXVI-16: unbounded recursion on `|`-separated command chains (exvi.c:399)
`do_command` recurses per `EXVI_COMMAND_BREAK_SEPARATOR` segment with no
depth cap; a line of many `|` segments exhausts the stack.

### EXVI-17: `exvi_popen` leaks a child (zombie) when `fdopen` fails (exvi_cmd.c:882-894)
After a successful fork, the `fdopen`-NULL path closes the fd and
returns without `kill`/`waitpid` (the malloc-fail path does reap).

### EXVI-18: unchecked `strndup` stored into the exec argv (exvi_cmd.c:793,801)
`exvi_tokenize_command` stores possibly-NULL `strndup` results into
`argv`, later `execvp(argv[0], argv)` in the child - NULL element ->
child-side crash.

### EXVI-19: signed-overflow UB in ex address arithmetic (exvi_parse.c:396-397,476-478)
`strtol` results land in `int addr` and `addr += sign*(int)offset` with
no pre-cast range check; `.+99999999999` overflows int (UB).  Value is
clamped afterward so no OOB (**[VERIFIED]** huge address -> "Bad
address").

### EXVI-20: substitute accepts an invalid delimiter (exvi_cmd.c:672)
`delim = *ptr;` takes any char after `s` with no check that it is a
legal (non-alnum, non-`\`, non-newline) delimiter; `sxfooxbarx` and a
`\` delimiter (which then collides with EXVI-06's escape handling) are
accepted.

### EXVI-21: `save_undo` runs before the substitute command is validated (exvi_cmd.c:668)
A malformed/no-op `s` still records an undo snapshot, so a later `u`
appears to do nothing (undo-history pollution).

### EXVI-22: int overflow in `line_array` growth size math (exvi_buffer.c:19-22)
`sizeof(ptr)*(size_t)new_cap` and `new_cap*=2` can wrap toward INT_MAX
on a 32-bit target, undersizing the allocation.  Needs ~10^9 lines;
practically unreachable but unguarded.

### EXVI-23: possible `perror(NULL)` on `:r` with no filename (exvi_session.c:1197,1286)
With no arg and `b->filename==NULL`, the failure branch calls
`perror(NULL)`.

### EXVI-24: `%` go-to-percent multiply overflows int (exvi_visual.c:6028)
`(percent_count*b->line_count + 99)/100` overflows for a huge count on
a large file; re-clamped afterward, so wrong target line, no OOB.

### EXVI-25: byte-wise `~` and `r` corrupt UTF-8 (exvi_visual.c:6375,6448)
`vi_toggle_case`/`vi_replace_char` rewrite `text[i]` one byte at a time
via ctype/`toupper`, so `~`/`r` over a multibyte char produces invalid
UTF-8.  Correctness only (indices are bounds-checked).

### EXVI-26: unguarded `cursor_col <= cur->len` invariant in insert/split (exvi_visual.c:6147,6506)
`vi_insert_char`'s `memcpy(text+cursor_col+1, ..., len-cursor_col+1)`
and `vi_split_line` would underflow `size_t` into a giant copy if
`cursor_col > len`.  No reachable trigger found (all insert-cursor paths
keep the invariant), but it is assumed, not asserted.

### EXVI-27: `isprint()`/`isspace()` on a raw int key (exvi_visual.c:6671,7549,7636,8211)
`key` can be 128-255; passing a non-`unsigned char`/non-EOF value to
ctype is UB.  In practice bounded to 0-255; cast to `unsigned char`.

### EXVI-28: Replace-mode journal push return ignored under OOM (exvi_visual.c:516)
`vi_record_replace_*` ignore `vi_push_replace_edit`'s -1, so under
memory pressure a Backspace in `R` mode restores the wrong byte.

### EXVI-29: large repeat counts busy-loop (responsiveness/DoS) (exvi_visual.c:1826)
`999999999x`/`w`/`h` run literal `while (count--)` loops, pinning the
editor for seconds uninterruptibly (count is saturated, so no
allocation risk).

### EXVI-30: display-column accumulation can overflow int (exvi_visual.c:1230)
`vi_display_col_for_index` sums tab widths into an `int`; a line of tens
of millions of tabs overflows to negative, corrupting horizontal
scroll.  No OOB.

### EXVI-31: cross-line bracket text-object gives up when cursor is on a bracket (exvi_visual.c:3705)
`vi_find_scanned_cross_bracket_span` early-returns -1 when the cursor
char is itself a bracket - asymmetric with the sibling helper; a
`%`-style span fails in that spot.  Functional edge case.

## Verification method
Host `ex`/`vi` were linked from the native `libexvi.a` + frontend
objects (working around EXVI-04) and driven with scripted ex sessions.
Reproduced: EXVI-01 (SIGSEGV on the three `:visual` startup paths),
EXVI-03 (TAGPWNED via a crafted tags file), EXVI-06 (`s/a\.b/Z/` matched
`aXb`), EXVI-07 (`[&]` literal), EXVI-08 (`2,3t2` duplicated the wrong
line while `m` rejects dest-in-range), EXVI-19 (huge address clamps to
"Bad address").  The remaining findings are on OOM / ENOSPC / signal /
32-bit-overflow paths confirmed by source inspection.
