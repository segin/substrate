# cpio(1) audit — 2026-11-14

Scope: `usr.bin/cpio/` (`cpio.c`, 835 lines; `Makefile`, `README.md`,
`REQ-TO-TEST`, `tests/`), `usr.man/man1/cpio.1`, `usr.man/man5/cpio.5`.

Judge: POSIX where it speaks (cpio left the utilities in POSIX.1-2001; its
`odc` header survives as the pax "cpio interchange format"), otherwise GNU
cpio 2.15.  The safe-extraction defaults (no absolute paths, no `..`) are a
deliberate Substrate choice and are kept.

**Confirmed** means reproduced on a host build, side by side with GNU cpio
2.15.  **Read** means found by reading the code; the failure follows directly
from it.

Requirements are written EARS-style: *When* \<trigger\>, cpio *shall*
\<response\>.

## Critical — wrong data or silent failure

- [ ] **CPIO-1 Copy-in from a pipe, without `-H`, extracts nothing and exits 0.**
  Confirmed: `cat a.cpio | cpio -i` → rc 0, empty directory.
  `detect_fmt()` reads 6 bytes and then `lseek(fd, 0, SEEK_SET)`; on a pipe
  the seek fails, it returns `FMT_NEWC`, and the 6 magic bytes are gone, so
  the first header fails to parse and the loop ends quietly (see CPIO-3).
  *When* the archive is not seekable, cpio *shall* detect the format from
  bytes it keeps and then parses, without seeking.

- [ ] **CPIO-2 `-t` lists nothing unless `-v` is given.**
  Confirmed: `cpio -it < a.cpio` prints nothing; GNU prints the names.
  `extract_entry()` prints only `if (opt->verbose)`.
  *When* `-t` is given, cpio *shall* write each member name to standard
  output; *when* `-tv`, an `ls -l`-style line.

- [ ] **CPIO-3 A bad, truncated or unrecognised archive ends with exit 0 and no message.**
  Confirmed: a 150-byte truncation, a file of text, and a GNU `bin` archive
  all exit 0 having extracted nothing or only part.
  `mode_in()` does `if (r < 0) break;` for every header failure, end of input
  included.  Data read failures (`read_all` in `extract_entry`) set status 2
  but print nothing.
  *When* a header is malformed, or input ends before `TRAILER!!!`, cpio *shall*
  diagnose it and exit non-zero; *when* the input is junk between members, it
  *shall* resynchronise on the next magic and report the bytes skipped, as GNU
  does.

- [ ] **CPIO-4 Hard links are never restored on copy-in, and links in GNU archives lose their data.**
  Confirmed.  With ours: `a` gets the data and `b` becomes a separate empty
  file.  With a GNU newc archive, where the data rides on the *last* link:
  `a` becomes empty and `b` has the data, as two inodes.
  `extract_entry()` has no link table: every regular member is opened with
  `O_TRUNC` and gets its own inode.
  *When* a member has `nlink > 1` and its (dev, ino) was seen before, cpio
  *shall* `link()` it to the earlier name; for newc, *when* the data arrives
  on a later member, it *shall* create the deferred names as links to that
  one.  `tests/.../test_links.sh` hides this behind `|| true`.

- [ ] **CPIO-5 Copy-out writes newc hard links data-first, not data-last.**
  Read.  `write_entry()` puts the data on the first link and size 0 on the
  rest; the newc convention (GNU, the kernel's initramfs reader) is size 0
  on all but the last.  For odc and bin the convention is the data on
  *every* link, and ours writes size 0 there too.  GNU 2.15 copes with both
  by inode matching, but a reader that does not, extracts empty files.
  *When* writing newc, cpio *shall* defer a multiply-linked file's data to
  its last link in the archive; *when* writing odc or bin, it *shall* write
  the data on every link.

- [ ] **CPIO-6 Files of 4 GiB or more are silently truncated on copy-out.**
  Confirmed: a 4 GiB + 1 file archives as 1 byte, rc 0.
  `data_size` is `uint32_t` and takes `st.st_size` unchecked, for odc as well
  (whose 11-digit octal field holds up to 8 GiB).
  *When* a file is larger than the format's size field, cpio *shall*
  diagnose it, skip the member and exit non-zero.

- [ ] **CPIO-7 A file that shrinks while being archived corrupts the rest of the archive.**
  Read.  The header promises `data_size` bytes; `copy_out_file_data()` stops
  at the early EOF and returns -1, and every later member is misaligned.
  An archive write error is also only "minor" (status 1), and writing
  carries on after a partial header.
  *When* a file yields fewer bytes than its header says, cpio *shall* pad
  with zeros to the promised size and warn; *when* writing the archive
  fails, it *shall* stop with exit 2.

- [ ] **CPIO-8 GNU (little-endian) `bin` archives cannot be read; ours are big-endian.**
  Confirmed: a GNU `-H bin` archive extracts nothing, rc 0; GNU reading ours
  says "archive header has reverse byte-order".
  The old binary header is in the writer's byte order; x86 is little-endian.
  *When* writing `bin`, cpio *shall* use native (little-endian) order;
  *when* reading, it *shall* accept either order by the magic.

## High — security

- [ ] **CPIO-9 The default "safe" extraction can be steered out of the tree through a symlinked directory.**
  Confirmed: an archive holding `lnk -> /abs/victim`, then one holding
  `lnk/pwn`, writes `/abs/victim/pwn` with no options.  The same works
  inside one archive.  GNU 2.15 does the same, but it makes no safety claim;
  ours promises safe extraction by default.
  `path_is_safe()` only checks the name's text; `unlink()` and `O_NOFOLLOW`
  only guard the last component.
  *When* safe extraction is on, cpio *shall* refuse to create a member any of
  whose leading components resolves to a symlink.  Use `openat` with
  `O_NOFOLLOW | O_DIRECTORY` component by component, or create the symlinks
  last.

- [ ] **CPIO-10 Pass mode writes through a symlink at the destination.**
  Confirmed: `pd/f -> ../outside/target`, then `echo f | cpio -p pd`, writes
  `outside/target`.  `copy_one_path()` opens the target with `O_TRUNC` and
  without `unlink()` or `O_NOFOLLOW`.  It also keeps an existing file's old
  mode.
  *When* the destination exists and is not a directory, cpio *shall* remove
  it before creating the copy.

- [ ] **CPIO-11 A symlink member's size is used unbounded.**
  Read.  `malloc(e->filesize + 1)` uses the 33-bit odc size; on i386,
  `(size_t)` truncates it, and the read and NUL use different widths.  A
  hostile header gets a huge allocation, or a desynchronised stream.
  *When* a symlink member's size exceeds `PATH_MAX`, cpio *shall* diagnose it
  and skip its data.

## Medium — wrong behaviour against GNU

- [ ] **CPIO-12 Extracted modes are masked by the umask; setuid, setgid and sticky bits are lost.**
  Confirmed: archived 2775 → extracted 755 (GNU: 2775).
  Files, directories and fifos are created with `mode & 07777` and never
  `chmod`ed.  Pass mode has the same fault.
  *When* a member is extracted, cpio *shall* set its mode to the archived
  permissions (after any `chown`, which clears setuid).

- [ ] **CPIO-13 Existing newer files are overwritten; `-u` does nothing.**
  Confirmed: a newer `f` on disk was replaced.  GNU refuses ("newer or same
  age version exists") unless `-u` is given.  `force_overwrite` is set and
  never read.
  *When* the file on disk is not older than the member and `-u` is absent,
  cpio *shall* keep it and say so.

- [ ] **CPIO-14 Read-only directories in the archive break their own contents; `-m` on directories is lost.**
  Confirmed: a 555 directory, then `ro/z` → "Permission denied", rc 1.
  Directories are created with their final mode at once, and their mtime is
  set before their contents change it.
  *When* extracting a directory, cpio *shall* create it writable and apply
  its mode (and, with `-m`, its mtime) once the archive is read.

- [ ] **CPIO-15 Ownership is not restored as root.**
  Read.  `lchown()` runs only with `--numeric-owner`; GNU restores the
  archived uid/gid by default when run as root.  `-R` applies only to `-o`;
  GNU applies it to `-i` and `-p`, and takes names and `user:`, `user.group`
  forms.  `parse_owner()` uses `strtol` with no validation.
  *When* run by root, cpio *shall* restore the archived owner, and *shall*
  accept `-R` names in every mode.

- [ ] **CPIO-16 Pass mode skips fifos, devices and hard links, and is silent about it.**
  Confirmed for fifos: `-p` of a fifo creates nothing, rc 0.
  `copy_one_path()` returns 0 for anything not a directory, symlink or
  regular file.  There is no hard-link table (each link becomes a copy) and
  no `-l`.  An unopenable source still creates an empty target.  `-v`
  reports a name even when its copy failed.

- [ ] **CPIO-17 Pattern operands to `-i` are ignored.**
  Confirmed: `cpio -id d/g` extracted `f` too.
  *When* patterns are given, cpio *shall* select members by `fnmatch()`, and
  *with* `-f`, those that do not match.

- [ ] **CPIO-18 Options missing against GNU.**
  Read.  Not implemented:
  - `-c` (portable ASCII);
  - `-I`/`-O` (archive file);
  - `-L` (follow symlinks), `-l` (link in pass mode), `-a` (reset atime);
  - `-B` and `-C` (block size; `-C` is taken and its argument dropped);
  - `-0`/`--null`, `--quiet`, `--no-preserve-owner`, `-D` (directory), `-r`
    (rename), `-b`/`-s`/`-S` (swap), `-E` (pattern file), `-V`;
  - the `crc` format (070702 is read but its checksum is never checked, and
    it cannot be written), and `ustar`/`tar`/`hpodc`.

  Long options are stripped from `argv` before `getopt`, so an
  option-argument such as `-F --insecure` is misread.  `--` is not
  recognised there.  Conflicting modes (`-o -i`) silently take the last
  one, and stray operands to `-o` are ignored.

- [ ] **CPIO-19 The archive is not padded to a block.**
  Read.  GNU pads to 512 bytes and reports "N blocks" on standard error.
  Readers do not need the padding, but tape and block devices do.

## Low — robustness, limits, hygiene

- [ ] **CPIO-20 Device numbers on a host build use the 8-bit fallback macros.**
  `<sys/sysmacros.h>` is not included, and glibc no longer defines
  `major`/`minor` in `<sys/types.h>`, so the local 8-bit fallbacks are used.
  newc device majors and minors above 255 are corrupted.
- [ ] **CPIO-21 Header fields overflow silently.**
  odc `mtime` before 1970, or past 2242, prints more than 11 octal digits on
  LP64 and shifts the header.  `nlink` is not masked to 6 digits.  `bin`
  truncates `dev`, `ino` and `uid` to 16 bits, and newc truncates `ino` to
  32, so inode collisions alias hard links once CPIO-4 is fixed.  newc
  writes `nlink` 1 for directories.
- [ ] **CPIO-22 Path list lines longer than `PATH_MAX` - 1 are split into two bogus names.**
  This is in `fgets` in `mode_out` and `mode_pass`.
- [ ] **CPIO-23 `-t` applies the extraction safety checks.**
  A listing of an archive with absolute names prints "blocked unsafe path"
  and exits 1.
- [ ] **CPIO-24 `--no-overwrite` is checked with `access()`, which follows symlinks.**
  A dangling symlink counts as absent and is replaced.
- [ ] **CPIO-25 Some I/O is needlessly slow.**
  - Header padding is read one byte per `read()`.
  - The hard-link list is searched linearly: O(n²) for n links.
  - The sparse writer seeks over every zero run, even a single byte.
- [ ] **CPIO-26 Diagnostics are inconsistent.**
  Many lack the `cpio:` prefix ("unknown format:", "invalid owner:", bare
  `perror` tags such as "open", "mkdir -p", "archive write").  `opterr = 0`
  hides which option was bad.  `-v` on copy-in prints `./name` rather than
  `name`.
- [ ] **CPIO-27 Code hygiene.**
  - Hand-written `extern` prototypes (`symlink`, `mkfifo`, `mknod`,
    `lchown`, `utimensat`) and local `AT_*`/`PATH_MAX` fallbacks, against the
    no-manual-externs rule.
  - Dead code: `mode_in()`'s `chdir(pass_dir)` (`pass_dir` is only set for
    `-p`).
  - Small leaks: `new_argv` on the `-H`/`-R` error returns; the link list.
- [ ] **CPIO-28 Substrate declares `utimensat` only in `<sys/time.h>`.**
  POSIX puts it in `<sys/stat.h>` (a libc header gap, not cpio's; listed so
  removing CPIO-27's `extern` does not break the target build).

## Tests and documentation

- [ ] **CPIO-29 The tests are in the wrong place, never run, and assert little.**
  - They live in `usr.bin/cpio/tests/` (the rule is `tests/usr.bin/cpio/`).
  - No Makefile target runs them, and CI does not.
  - `test_links.sh` ends in `|| true`.
  - `test_safety.sh`'s first case archives paths that do not exist.
  - `test_interop.sh` only checks that the host cpio can list a newc archive.
  - Nothing covers odc or bin, pipes, `-t` output, `-p`, `-m`, modes,
    errors, or interop in the GNU → ours direction.
  - `REQ-TO-TEST` maps to these tests and goes with them.
- [ ] **CPIO-30 `cpio.1` is thin and partly wrong.**
  - It omits `--insecure`, `--absolute-paths` and `--format=`.
  - It calls `-u` "accepted for compatibility".
  - It has `.Ex -std` and a contradictory 0/1/2 list.
  - Its STANDARDS section claims a "POSIX cpio model".
  - It has no DIAGNOSTICS or EXAMPLES section, and SEE ALSO lacks
    `cpio(5)`.
- [ ] **CPIO-31 `cpio.5` gives no header layouts.**
  It has no field widths or encodings for newc, odc or bin, no padding rules
  and no trailer, and nothing on the hard-link conventions (CPIO-5).
  `README.md` duplicates the man page.

## Suggested order

1. CPIO-29 first: move the tests and wire them into CI, so every later fix
   lands with a test.
2. Then the data-loss group: CPIO-1, 3, 4, 5, 6, 7 and 8.
3. Then security: CPIO-9, 10 and 11.
4. Then GNU behaviour: CPIO-12 to 19.
5. Then the rest.

CPIO-4, 5, 9, 12, 13, 14 and 16 all touch the extract and copy paths and the
option parser.  As with pr, one rewrite may be cleaner than piecemeal
patches; if so, that should be agreed before starting, so the commits can
be planned (one per fix, or one rewrite).
