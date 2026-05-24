# `mv` / `rm` Specification — POSIX.1-2024 + GNU + BSD

**Scope.** Substrate's `bin/mv` and `bin/rm` SHALL implement the complete
POSIX.1-2024 (IEEE Std 1003.1-2024) command-line surface, **plus** the
GNU coreutils and BSD (FreeBSD/macOS) extensions enumerated below. When
GNU and BSD diverge, the **BSD** behavior wins. POSIX is the floor;
extensions are additive unless they conflict, in which case BSD takes
precedence.

**Reference baseline:**
- POSIX.1-2024 (`mv(1)`, `rm(1)`)
- FreeBSD 14 `mv(1)`, `rm(1)`
- GNU coreutils 9.x `mv(1)`, `rm(1)`

**LLM-actionable conventions.** Requirement IDs are stable. EARS form:
`U` = ubiquitous, `E` = event-driven, `S` = state-driven, `O` = optional
feature, `X` = unwanted-behavior. User stories follow Connextra
("As a … I want … so that …"). The tasklist at the end is the
execution plan; each task is a checkbox, scoped to a single commit's
worth of work.

---

## 1. User Stories

### 1.1 `mv`

- **US-MV-01.** As a user, I want `mv a b` to atomically replace `b`
  with `a` so that I can rename files without windowed inconsistency.
- **US-MV-02.** As a user, I want `mv a b/` to place `a` inside the
  existing directory `b` so that I can move files into directories.
- **US-MV-03.** As a user, I want `mv a b c d/` to move multiple
  sources into a single target directory in one command.
- **US-MV-04.** As a cautious user, I want `mv -i` to prompt before
  overwriting an existing target so that I don't lose data by mistake.
- **US-MV-05.** As a cautious user, I want `mv -n` to refuse to
  overwrite existing targets so that I can safely move into
  populated trees.
- **US-MV-06.** As a script author, I want `mv -f` to be unconditional
  so that `mv -fi` resolves to "force" (last flag wins, per POSIX).
- **US-MV-07.** As an operator, I want `mv -v` to print each successful
  move so that I can audit batch operations.
- **US-MV-08.** As a BSD user, I want `mv -h` to treat a symlinked
  target directory as a plain symlink (rename the link, don't traverse
  into it).
- **US-MV-09.** As a user moving across filesystems, I want `mv` to
  fall back to a `cp -PRp` + `rm -rf` sequence when `rename(2)` fails
  with `EXDEV`.
- **US-MV-10.** As a backup-conscious user, I want `mv --backup=...`
  to preserve overwritten targets under a numbered or simple suffix.
- **US-MV-11.** As a script author, I want `mv -t DIR src...` to
  put `-t` first so I can compose with `xargs -I{}`.
- **US-MV-12.** As a script author, I want `mv -T src dst` to treat
  `dst` as a normal file even when it's an existing directory, so I
  can replace a whole directory atomically.
- **US-MV-13.** As an interactive user, I want `mv --update=...` so
  that `mv` only overwrites when the source is newer.

### 1.2 `rm`

- **US-RM-01.** As a user, I want `rm a b c` to remove three files
  in one invocation.
- **US-RM-02.** As a user, I want `rm -r dir` to remove a directory
  tree recursively.
- **US-RM-03.** As a cautious user, I want `rm -i` to prompt before
  every deletion.
- **US-RM-04.** As a cautious user, I want `rm -I` to prompt only
  once when removing more than three files or recursively, so I
  get a sanity check without a flood of prompts (POSIX-2024).
- **US-RM-05.** As a script author, I want `rm -f` to silently ignore
  missing files and never prompt.
- **US-RM-06.** As an operator, I want `rm -v` to print each removed
  path so that I can audit batch deletions.
- **US-RM-07.** As an operator on a multi-mount system, I want
  `rm --one-file-system` (or BSD `-x`) to refuse to cross mount
  points during recursion.
- **US-RM-08.** As a safety-first user, I want `rm` to refuse to
  delete `/` unless `--no-preserve-root` is given.
- **US-RM-09.** As a user removing empty directories, I want
  `rm -d` to act like `rmdir` for empty directories (BSD/GNU).
- **US-RM-10.** As a security-conscious user, I want `rm -P` to
  overwrite each file with multiple patterns before unlinking, so
  the data is not trivially recoverable (BSD).
- **US-RM-11.** As an end user behind `rm`, I want descriptive,
  POSIX-conformant exit codes (0 success, >0 failure) so that
  scripts can chain reliably.

---

## 2. EARS Requirements

### 2.1 Common (apply to both `mv` and `rm`)

| ID | Form | Statement |
|---|---|---|
| `R-COMMON-01` | U | The utility SHALL accept `--` as an end-of-options sentinel and treat all subsequent arguments as operands. |
| `R-COMMON-02` | U | The utility SHALL accept the GNU `--long-option` syntax and the BSD/POSIX short-cluster syntax (`-rf`). |
| `R-COMMON-03` | U | The utility SHALL exit with status `0` on full success, `>0` on any operand failure. |
| `R-COMMON-04` | U | The utility SHALL emit all diagnostics to stderr, prefixed `progname: `. |
| `R-COMMON-05` | U | The utility SHALL respect the `LC_*` environment variables for collation of any user-visible strings, falling back to the C locale on error. |
| `R-COMMON-06` | E | When an unknown option is encountered, the utility SHALL print a one-line usage to stderr and exit `2` (BSD; GNU prints usage then exits `1`, but BSD wins). |
| `R-COMMON-07` | E | When `--help` is given, the utility SHALL print a usage summary to stdout and exit `0`. |
| `R-COMMON-08` | E | When `--version` is given, the utility SHALL print `progname (substrate) VERSION` and exit `0`. |
| `R-COMMON-09` | X | If a path operand is an empty string, then the utility SHALL emit a diagnostic and treat it as an operand failure (POSIX). |

### 2.2 `mv` Requirements

#### 2.2.1 Synopsis & operand parsing
| ID | Form | Statement |
|---|---|---|
| `R-MV-01` | U | `mv` SHALL accept the synopses `mv [OPTION]... SOURCE DEST` and `mv [OPTION]... SOURCE... DIRECTORY` and `mv [OPTION]... -t DIRECTORY SOURCE...`. |
| `R-MV-02` | E | When invoked with fewer than two operands and no `-t`, `mv` SHALL emit the usage and exit `1`. |
| `R-MV-03` | E | When `-t DIRECTORY` is given, all positional operands SHALL be sources, and DIRECTORY is the target. |
| `R-MV-04` | E | When `-T` is given, `mv` SHALL refuse to treat DEST as a directory even if it exists as one, exiting with `1` if the resulting operation would require directory semantics. |
| `R-MV-05` | U | `mv` SHALL strip trailing slashes from source operands prior to deriving the basename for directory targets. |

#### 2.2.2 Overwrite policy
| ID | Form | Statement |
|---|---|---|
| `R-MV-10` | U | The effective overwrite policy SHALL be the LAST of `-f`/`-i`/`-n` on the command line (POSIX). |
| `R-MV-11` | S | While the prompt mode is "interactive" and the target exists, `mv` SHALL print `mv: overwrite 'TARGET'? ` to stderr and read a line from stdin. |
| `R-MV-12` | E | When the response to a `-i` prompt begins with `y` or `Y` (locale-aware where supported), `mv` SHALL proceed with the rename; otherwise it SHALL skip the operand and continue. |
| `R-MV-13` | S | While prompt mode is "never overwrite" (`-n`) and the target exists, `mv` SHALL skip the operand and continue, returning success for that operand. |
| `R-MV-14` | E | When `mv` skips an operand under `-n`, it SHALL NOT consider that a failure for exit-status purposes. |
| `R-MV-15` | X | If the target exists and is not writable and stdin is a TTY (and `-f` is not in effect), then `mv` SHALL prompt `mv: override mode WRONLY 'TARGET'? ` (BSD). |
| `R-MV-16` | O | Where `--update[=WHEN]` is supported and the source is not newer than the target, `mv` SHALL skip the operand without diagnostic. |

#### 2.2.3 Rename + cross-mount fallback
| ID | Form | Statement |
|---|---|---|
| `R-MV-20` | U | `mv` SHALL attempt `rename(2)` first for every operand. |
| `R-MV-21` | E | When `rename(2)` fails with `EXDEV`, `mv` SHALL fall back to a copy-and-delete sequence equivalent to `cp -PRp SRC DEST && rm -rf SRC`, preserving mode, mtime, atime, ownership where possible, and symlinks as-is. |
| `R-MV-22` | E | When the copy phase of the EXDEV fallback fails partially, `mv` SHALL leave the destination as-is (no rollback) and SHALL NOT remove the source. |
| `R-MV-23` | X | If the copy phase succeeds but the source removal fails, then `mv` SHALL diagnose and exit non-zero, leaving both copies on disk. |
| `R-MV-24` | E | When the source is a symlink, `mv` SHALL move the link itself (never dereference). BSD `-h` is the default behavior for moves; the `-h` flag controls treatment of the *target* as a symlink. |
| `R-MV-25` | E | When the source and target name resolve (via realpath) to the same file system object, `mv` SHALL diagnose `'SRC' and 'DEST' are the same file` and skip the operand without error if both are the same name, or with error otherwise. |

#### 2.2.4 BSD `-h` behavior
| ID | Form | Statement |
|---|---|---|
| `R-MV-30` | O | Where `-h` is given and DEST is a symbolic link to a directory, `mv` SHALL treat DEST as a plain file (i.e., replace the symlink, do not traverse it). |

#### 2.2.5 GNU `--backup` family
| ID | Form | Statement |
|---|---|---|
| `R-MV-40` | O | Where `-b` or `--backup[=CONTROL]` is supported, `mv` SHALL back up an existing target before overwriting. |
| `R-MV-41` | O | Where `-S SUFFIX` or `--suffix=SUFFIX` is supported, `mv` SHALL use SUFFIX in place of `~` for simple backups. |
| `R-MV-42` | O | Where `--backup=numbered` is supported, `mv` SHALL produce backup names of the form `TARGET.~N~`. |
| `R-MV-43` | O | Where `--backup=existing` is supported, `mv` SHALL produce numbered backups if any numbered backups exist, else simple. |
| `R-MV-44` | O | Where `--backup=none` or `--backup=off` is supported, `mv` SHALL NOT create a backup (effectively `-b` no-op). |
| `R-MV-45` | E | When `-b` is given without `--backup=CONTROL` and `$VERSION_CONTROL` is set, the value of `$VERSION_CONTROL` SHALL select the backup style. |

#### 2.2.6 Verbose / debug
| ID | Form | Statement |
|---|---|---|
| `R-MV-50` | O | Where `-v` or `--verbose` is given, `mv` SHALL print one line per successful operand of the form `renamed 'SRC' -> 'DEST'` (GNU style; BSD `mv -v` is also `SRC -> DEST` with no `renamed ` prefix — substrate uses GNU prefix). |

### 2.3 `rm` Requirements

#### 2.3.1 Synopsis & operand parsing
| ID | Form | Statement |
|---|---|---|
| `R-RM-01` | U | `rm` SHALL accept the synopsis `rm [-fiIrRvd] [-P] [--one-file-system] [--preserve-root[=all]] [--no-preserve-root] FILE...`. |
| `R-RM-02` | E | When invoked with no operands and no `-f`, `rm` SHALL emit the usage and exit `1`. |
| `R-RM-03` | E | When invoked with no operands and `-f`, `rm` SHALL exit `0` silently (POSIX). |

#### 2.3.2 Prompting
| ID | Form | Statement |
|---|---|---|
| `R-RM-10` | U | The effective prompt mode SHALL be the LAST of `-f`/`-i`/`-I` on the command line. |
| `R-RM-11` | S | While prompt mode is "always" (`-i`), `rm` SHALL prompt before each removal of the form `rm: remove FILE? `. |
| `R-RM-12` | S | While prompt mode is "once" (`-I`) and either (a) more than three operands are given or (b) recursion is enabled, `rm` SHALL prompt once `rm: remove N argument(s)? ` before processing any operand. |
| `R-RM-13` | E | When the `-I` prompt is answered other than `y`/`Y`, `rm` SHALL exit `0` without removing anything. |
| `R-RM-14` | E | When the response to any prompt begins with `y` or `Y`, `rm` SHALL proceed; any other response SHALL skip that operand without error. |
| `R-RM-15` | X | If `-f` is in effect, then `rm` SHALL NOT prompt under any condition and SHALL silently treat `ENOENT` as success. |

#### 2.3.3 Recursion + safety
| ID | Form | Statement |
|---|---|---|
| `R-RM-20` | O | Where `-r` or `-R` is given, `rm` SHALL recursively remove directories and their contents. |
| `R-RM-21` | U | Without `-r`/`-R` or `-d`, `rm` SHALL refuse to remove directories and emit `rm: cannot remove 'DIR': Is a directory`. |
| `R-RM-22` | O | Where `-d` is given, `rm` SHALL remove empty directories without recursion (acts like `rmdir`). |
| `R-RM-23` | U | `rm` SHALL traverse directories using `openat`/`fdopendir`/`unlinkat` (file-descriptor-relative) to be safe against active path-component races. |
| `R-RM-24` | U | `rm` SHALL refuse to descend through a symlinked directory in recursive mode. |
| `R-RM-25` | S | While `--preserve-root` is in effect (default), `rm` SHALL refuse to delete `/` or any operand that resolves to `/`. |
| `R-RM-26` | O | Where `--preserve-root=all` is given, `rm` SHALL additionally refuse to descend across mount points. |
| `R-RM-27` | O | Where `--no-preserve-root` is given, `rm` SHALL allow removal of `/`. |
| `R-RM-28` | O | Where `--one-file-system` (GNU) or `-x` (BSD) is given, `rm` SHALL skip operands and child directories whose `st_dev` differs from the top operand's `st_dev`. |
| `R-RM-29` | X | If any path operand resolves to `.` or `..` as the final component, then `rm` SHALL refuse and emit `rm: refusing to remove '.' or '..'`. |

#### 2.3.4 BSD secure-erase
| ID | Form | Statement |
|---|---|---|
| `R-RM-30` | O | Where `-P` is given, `rm` SHALL overwrite regular files with three passes (0xFF, 0x00, 0xFF, `fsync`) before unlinking. |
| `R-RM-31` | E | When `-P` cannot determine the file size (e.g., special file), `rm` SHALL skip the overwrite phase and proceed with unlink. |
| `R-RM-32` | E | When `-P` overwrite fails mid-pass, `rm` SHALL diagnose and continue with unlink (matching FreeBSD behavior). |

#### 2.3.5 Verbose
| ID | Form | Statement |
|---|---|---|
| `R-RM-40` | O | Where `-v` is given, `rm` SHALL print `'PATH'` (single-quoted, BSD format) to stdout per successful removal. |

---

## 3. Test Plan

Each requirement maps to at least one test. Test sources live under
`tests/bin/mv/` (new) and `tests/bin/rm/` (extend). Build targets:

```
make -C bin/mv  unit integration property
make -C bin/rm  unit integration property fuzz-run
```

- **Unit tests** target individual modules (options parser, prompt
  helper, path utilities, backup-name resolver, EXDEV fallback engine).
- **Integration tests** drive the built binary against a sandbox
  directory and assert filesystem state, exit code, and captured
  stderr/stdout. Reuse the existing host-runnable harness style from
  `tests/bin/rm/test_integration.sh`.
- **Property tests** generate random option clusters and short
  operand lists; the parser must classify each consistently with
  POSIX precedence rules.
- **Fuzz tests** feed pathological CLI strings to the option parser
  and require no crash within N seconds.

---

## 4. Execution Tasklist

The tasklist is the actionable plan. Each `[ ]` checkbox is roughly
one commit's worth.

### 4.1 `bin/mv` — full rewrite (existing 151-line monolith covers `-f` only)

#### Module split
- [ ] **MV-T01** Split `bin/mv/mv.c` into `mv.c` (main + dispatch),
  `mv_opts.c/h` (option parser), `mv_path.c/h` (path utilities,
  trailing-slash, basename, join, realpath compare), `mv_rename.c/h`
  (single-operand engine: rename + EXDEV fallback driver), `mv_prompt.c/h`
  (prompt helper shared with possible future utilities),
  `mv_backup.c/h` (GNU backup-name resolver).
- [ ] **MV-T02** Update `bin/mv/Makefile` to list the new modules in
  `SRCS` and to declare unit-test targets parallel to `bin/rm`.
- [ ] **MV-T03** Add `tests/bin/mv/` dir with skeletons for `test_opts.c`,
  `test_path.c`, `test_backup.c`, `test_integration.sh`, `test_property.py`.

#### Option parser (`mv_opts.c`) — implements R-MV-* parsing
- [ ] **MV-T10** Implement `mv_options_init` / `mv_parse_options` /
  `mv_options_free` with long+short option table. Match BSD ordering
  for short letters (`-finv`, `-h`, `-bTtSv`), GNU long names for
  everything else.
- [ ] **MV-T11** Implement last-of-{-f,-i,-n} wins semantics. Unit-test
  every order permutation.
- [ ] **MV-T12** Implement `--backup[=CONTROL]` with the GNU control
  vocabulary (`none|off`, `numbered|t`, `existing|nil`, `simple|never`)
  and the `$VERSION_CONTROL` fallback.
- [ ] **MV-T13** Implement `-S SUFFIX`, `--suffix=SUFFIX`. Default suffix
  is `~` unless `$SIMPLE_BACKUP_SUFFIX` is set.
- [ ] **MV-T14** Implement `-t DIR` / `--target-directory=DIR` and
  `-T` / `--no-target-directory` with mutual-exclusion check.
- [ ] **MV-T15** Implement `--strip-trailing-slashes` (GNU; default
  behavior under POSIX already strips for source operand basename
  derivation, but the GNU flag also strips for the destination).
- [ ] **MV-T16** Implement `--update[=WHEN]` with the GNU vocabulary
  (`all|none|none-fail|older`).
- [ ] **MV-T17** Implement `-v` / `--verbose`. Implement `--debug`
  as an alias for `--verbose` plus mtime/ctime preservation diagnostics.

#### Rename engine (`mv_rename.c`) — implements R-MV-2*
- [ ] **MV-T20** Implement `mv_rename_one(src, dst, opts)` — try
  `renameat2(AT_FDCWD, src, AT_FDCWD, dst, RENAME_NOREPLACE)` when
  `-n`, plain `renameat` otherwise.
- [ ] **MV-T21** On `EXDEV`, dispatch to `mv_cross_device(src, dst, opts)`
  which:
  - `lstat` source.
  - If symlink: `readlink` + `symlinkat` at dest, then `unlink` source.
  - If directory: recursive `cp -PRp` followed by `rm -rf`. Use the
    libc `nftw` framework or hand-rolled `openat` walk; preserve
    mode/uid/gid/atime/mtime where supported.
  - If regular file: stream copy via `read`/`write` with hole detection
    (`SEEK_HOLE`/`SEEK_DATA`); preserve mode/times; `fdatasync` before
    `close`.
  - If special file: `mknod` then `unlink` source.
- [ ] **MV-T22** Implement BSD same-file check via `realpath` (or
  stat-based dev/ino comparison if realpath fails). Print
  `mv: 'SRC' and 'DEST' are identical (not moved)` and skip without
  error.
- [ ] **MV-T23** Implement the BSD "override mode" prompt for
  unwritable targets when stdin is a TTY and `-f` is not set.

#### Backup module (`mv_backup.c`) — implements R-MV-4*
- [ ] **MV-T30** Implement `mv_backup_name(target, control, suffix)`
  returning a malloc'd path. For `numbered`, scan parent dir for
  existing `TARGET.~N~` files and pick the next `N`.
- [ ] **MV-T31** Implement `mv_perform_backup(target, control, suffix)`
  which `link()`s or `rename()`s the existing target to the backup
  name before the actual move.

#### Main driver (`mv.c`)
- [ ] **MV-T40** Replace the current `main` with: parse options;
  classify dispatch (2-arg vs N-arg vs `-t`); iterate operands;
  accumulate worst exit status.
- [ ] **MV-T41** Implement `-T` mutual exclusion and the directory-
  target detection branch.
- [ ] **MV-T42** Honor `-v` and `--debug` in the per-operand loop.

#### Man page
- [ ] **MV-T50** Author `usr.man/man1/mv.1` covering every option;
  cross-reference `cp(1)`, `rm(1)`, `rename(2)`, `renameat2(2)`.

#### Tests
- [ ] **MV-T60** `tests/bin/mv/test_opts.c` — option parser truth
  table including last-of-{f,i,n}, backup-control mapping, `-t`/`-T`
  mutual exclusion.
- [ ] **MV-T61** `tests/bin/mv/test_path.c` — trailing-slash stripping,
  basename derivation, same-file detection.
- [ ] **MV-T62** `tests/bin/mv/test_backup.c` — backup-name selection
  across all four CONTROL values and `$VERSION_CONTROL` precedence.
- [ ] **MV-T63** `tests/bin/mv/test_integration.sh` — golden cases:
  rename, rename across dir, EXDEV fallback (uses `tmpfs`), `-i` yes/no,
  `-n` skip, `-f` clobber, `-b` backup, `--update=older`, `-T` against
  existing dir, BSD `-h` against symlinked dir, `--strip-trailing-slashes`.
- [ ] **MV-T64** `tests/bin/mv/test_property.py` — random
  `mv -X -Y -Z A B C D` invocations, assert parser doesn't crash
  and rejects only invalid combinations.
- [ ] **MV-T65** Cross-link `tests/bin/mv` from `bin/mv/Makefile`'s
  `unit`, `integration`, `property` targets exactly as `bin/rm`
  does.

### 4.2 `bin/rm` — gap closure on top of existing implementation

Audit against §2.3:
- [ ] **RM-T01** Audit `rm_opts.c` against R-RM-10..14 — verify
  last-of-{f,i,I} wins (existing). Add tests if missing.
- [ ] **RM-T02** Implement BSD `-P` overwrite mode (R-RM-30..32).
  Three-pass overwrite (`0xFF`, `0x00`, `0xFF`), `fsync` between
  passes. New file `rm_scrub.c/h`.
- [ ] **RM-T03** Audit `rm_walk.c` for `openat`/`unlinkat` usage
  (R-RM-23) — convert any path-based `unlink` to `unlinkat` against
  the parent dirfd.
- [ ] **RM-T04** Implement `--preserve-root=all` (R-RM-26) — refuse
  to cross mount points during recursion regardless of
  `--one-file-system`.
- [ ] **RM-T05** Verify `--one-file-system` / `-x` symmetry; alias
  BSD `-x` to GNU `--one-file-system` if not already.
- [ ] **RM-T06** Verify R-RM-29 — refuse `.`/`..` as final
  component. Audit `rm_safety.c`.
- [ ] **RM-T07** Extend `usr.man/man1/rm.1` if any new options
  (`-P`, `--preserve-root=all`) need documentation.
- [ ] **RM-T08** Extend `tests/bin/rm/test_integration.sh` with
  scrub, `--preserve-root=all`, BSD `-x` alias coverage.

### 4.3 `strings` + `strip` via GNU binutils

Existing state: `usr.bin/strings/TASKLIST_STRINGS.md` and
`usr.bin/strip/TASKLIST_STRIP.md` outline substrate-native
implementations on top of `libelfobj`. Per the user's directive,
**switch to binutils-supplied implementations** and retire the
from-scratch plans.

`dist-toolchain/usr/bin/strings` and `dist-toolchain/usr/bin/strip`
already exist (built by `contrib/binutils` stage 2) and overlay onto
the rootfs via `build-rootfs.sh:install_to_dist()`. The work is to:

- [ ] **TOOLS-T01** Replace `usr.bin/strings/TASKLIST_STRINGS.md`
  and `usr.bin/strip/TASKLIST_STRIP.md` with a single
  `README.SUBSTRATE.md` per dir noting "shipped by
  contrib/binutils stage 2; this directory is a placeholder so
  `usr.bin/Makefile` SUBDIRS iteration stays consistent."
- [ ] **TOOLS-T02** Add minimal `usr.bin/strings/Makefile` and
  `usr.bin/strip/Makefile` that are no-op targets (`all:`, `clean:`,
  `install:`) so the recursive `make -C usr.bin` doesn't fail on
  the empty dirs.
- [ ] **TOOLS-T03** Update `build-rootfs.sh` if needed — verify
  the dist-toolchain overlay places `/usr/bin/strings` and
  `/usr/bin/strip` correctly (it does today; confirm and document).

---

## 5. Acceptance Criteria

The work is complete when:

1. `make -C bin/mv` and `make -C bin/rm` build cleanly under both
   the substrate cross toolchain and `NATIVE_BUILD=1`.
2. `make -C bin/mv test` and `make -C bin/rm test` pass.
3. Every requirement ID in §2 has at least one corresponding test
   case in §3's test plan.
4. `/usr/bin/strings` and `/usr/bin/strip` exist on the booted
   rootfs and report `GNU strings` / `GNU strip` for `--version`.
5. `man mv`, `man rm`, `man strings`, `man strip` all return
   substrate-installed pages.
6. The substrate boot self-test (booting the freshly built image)
   shows no regressions from this work.
