# bin/ coreutils audit — 2026-07

Security/correctness audit of a batch of Substrate userland utilities, read
by parallel per-utility auditors and cross-checked against source. Substrate's
primary target is x86 **32-bit** (`size_t`/`long`/pointer 32-bit, `off_t` and
`time_t` 64-bit), so integer/growth overflow (`cap*2`, `a*b`, off_t→size_t
truncation) is a first-class bug class. Utilities parse/act on untrusted input
(files, argv, /proc, dc/awk programs) and some run as root (chown/chgrp/chroot).

Findings are numbered `<UTIL>-NN`, ranked by severity within each utility.
Severity: HIGH (memory-unsafe / privilege / data-loss / silent-wrong-security),
MEDIUM (DoS / correctness / resource), LOW (robustness / spec deviation).

Verification is by ASan/UBSan host builds driven by crafted inputs, plus the
per-utility behavior checks, mirroring the bin/sh audit method.

## Cross-cutting theme — path-based descent TOCTOU (HIGH)

`cp`, `mv`, `chown`, `chgrp` all perform recursive descent and file mutation by
re-resolving the **full path string** at each step (`lstat(path)` → decide →
`chown(path)`/`open(path,O_CREAT|O_TRUNC)`/`opendir(path)`), which is the classic
`-R` root race: an attacker who owns an intermediate directory swaps a component
for a symlink between the check and the act, and the operation escapes the tree
(chown `/etc/shadow`, truncate an arbitrary file). **`rm` already does this
correctly** — fully fd-relative `openat(O_NOFOLLOW|O_DIRECTORY)` + `fstatat`
+ `unlinkat` descent — and is the model to port cp/mv/chown/chgrp onto.
Substrate's libc provides `openat`/`fstatat`/`fchownat`/`unlinkat`/`renameat`
and `O_NOFOLLOW`/`AT_SYMLINK_NOFOLLOW`/`AT_REMOVEDIR`/`AT_FDCWD`, so the fix is
available. Also shared: unbounded recursion (stack overflow) + one open DIR per
level (fd exhaustion) on deep trees.

---

## cp  (bin/cp/)

- **CP-01** HIGH `cp_copy.c:945` — default atomic copy creates the temp with hardcoded mode `0600`; ordinary `cp foo bar` yields a 0600 file (loses exec/group/other, ignores umask & source mode). Create temp with `src_mode & 0777`.
- **CP-02** HIGH `cp_preserve.c:225-251` — `-p` applies `chmod(src_mode & 07777)` (incl. setuid/setgid) even when the preceding `chown` failed (EPERM), yielding a setuid binary owned by the invoking user. Strip S_ISUID/S_ISGID when owner not established.
- **CP-03** MEDIUM `cp_copy.c:871-1059` — `--backup` is a no-op for regular files (`cp_copy_regular` never calls `cp_maybe_backup_destination`); the common overwrite case loses the original with no backup.
- **CP-04** MEDIUM `cp_atomic.c:110-114`/`cp_copy.c:1008` — on commit failure the fd is closed inside commit but caller only clears `dst_fd` on success → double-close on cleanup path.
- **CP-05** MEDIUM `cp_copy.c:1098` — `cp -r` creates dest dir with `src_mode & 0777` before populating; a read-only source dir (0555) makes all child creation fail EACCES. Create with u+wx, apply real mode at end.
- **CP-06** MEDIUM `cp_copy.c:1264-1268` — `-T`/`--no-target-directory` ignored; dir-vs-file decision taken purely from stat.
- **CP-07** MEDIUM `cp_copy.c:1131`/`:1107` — unbounded recursion + per-level open DIR in `cp -r` (stack/fd exhaustion on deep tree).
- **CP-08** MEDIUM `cp_copy.c:938,951` — TOCTOU: `stat`+`open` with no `O_NOFOLLOW`/`openat`; non-atomic dest `open(O_CREAT|O_TRUNC)` follows a planted symlink → clobber outside dir. [cross-cutting]
- **CP-09** LOW `cp_copy.c:1087` — `visited_dirs` cycle set never pruned; same dir named twice (or reachable twice) falsely reported ELOOP and skipped. Track ancestor chain, not global set.
- **CP-10** LOW `cp_copy.c:1021-1025` — non-atomic `close()` failure clears `dst_fd` before `out:`, so partial dest not unlinked.
- **CP-11** LOW `cp_copy.c:184-192` — overwrite prompt reads only `buf[0]`, doesn't drain stdin.
- Solid: `cp_parse_size` overflow guard; `cp_path_*` exact-malloc (no PATH_MAX truncation); write/read EINTR+short handling; hardlink hash map dev+ino correct; atomic temp `O_CREAT|O_EXCL`; same-file / dir-into-itself rejects.

## mv  (bin/mv/)

- **MV-01** HIGH `mv_rename.c:57` — cross-device `copy_regular` opens dst `O_WRONLY|O_CREAT|O_TRUNC` (no O_NOFOLLOW/O_EXCL); a symlink planted at dst is followed and the linked file truncated (worse than the `rename` it substitutes). [cross-cutting]
- **MV-02** HIGH `mv_rename.c:93-95` — `copy_symlink` `readlink` truncates a ≥PATH_MAX target silently, creates a wrong link, then the source is removed → silent data loss. Detect truncation.
- **MV-03** MEDIUM `mv_rename.c:139,155,49` — unbounded `copy_tree`/`remove_tree` recursion + 64 KiB stack `copy_regular` buffer → stack exhaustion mid cross-device move. Cap depth, malloc the buffer.
- **MV-04** MEDIUM `mv_rename.c:80-81,133,149` — chmod-before-chown in the copy fallback drops setuid/setgid. chown first, then chmod.
- **MV-05** MEDIUM `mv_rename.c:232,287` — race-prone `lstat`+`rename` defeats `-i`/`-n` and same-file checks; use `renameat2(RENAME_NOREPLACE)` for `-n`/`-i`.
- **MV-06** MEDIUM `mv_backup.c:149-158` (suffix from `mv_opts.c:106,127`) — crafted `-S`/`$SIMPLE_BACKUP_SUFFIX` (e.g. `/../../victim`) escapes the dir and clobbers an unrelated file. Reject `/` in suffix.
- **MV-07** LOW `mv_rename.c:186-194` — stale errno in cross-device diagnostics (best-effort chmod/chown overwrite it).
- **MV-08** LOW `mv.c:52,97,130` — `strip_trailing_slashes` option parsed but stripping is unconditional (dead flag).
- **MV-09** LOW `mv_backup.c:145` — numbered-backup `highest + 1u` wraps on 32-bit `unsigned long`.
- **MV-10** LOW `mv_rename.c:274-285` — with `-b`, backup done before rename; a non-EXDEV rename failure leaves dst "missing" (at dst~). Roll back on failure.
- **MV-11** LOW `mv_rename.c:111` — cross-device dir-onto-existing-dir merges via `mkdir EEXIST` then removes source (diverges from rename's ENOTEMPTY).
- Solid: `mv_path_join` exact-malloc; prompt EOF=no+drains; copy_regular partial-write/EINTR + close-error check; copy→remove only on success; numbered-backup name matching bounded.

## rm  (bin/rm/) — core rm -rf race is CORRECTLY DEFENDED (fd-relative)

- **RM-01** MEDIUM `rm_walk.c:189,151` — unbounded recursion + one open DIR/fd per level; deep tree hits RLIMIT_NOFILE (partial delete) then stack overflow. Cap depth / recycle fds.
- **RM-02** MEDIUM `rm_walk.c:193-196,285-288` — first child failure `break`s the readdir loop, abandoning all remaining siblings; GNU continues. Use `continue`, record failure.
- **RM-03** MEDIUM `rm_walk.c:311 vs :144` — `--one-file-system` boundary dev taken from pre-`openat` `fstatat`, never re-verified against the opened fd; rename swap defeats the boundary limiter. `fstat` the fd.
- **RM-04** LOW `rm_walk.c:211,371,361` — residual name-based `unlinkat(AT_REMOVEDIR)`/scrub swap race (cannot escape, but a swapped-in empty dir/file can be removed/scrubbed). Document / re-fstatat before.
- **RM-05** LOW `rm_walk.c:466-480` — default `--preserve-root` refuses ALL mountpoint dirs; GNU only under `=all`/`--one-file-system`. Gate on preserve_root_all/one_file_system.
- **RM-06** LOW `rm_scrub.c:31-38` — `pass()` can infinite-loop on `write()==0`; only scrubs stat-time size (no re-fstat/ftruncate). Guard w==0.
- **RM-07** LOW `rm.c:62-66` — `-I` on non-tty/EOF stdin returns "no" → silent no-op. isatty gate / proceed.
- **RM-08** LOW `rm_walk.c:417`/`rm_safety.c:95` — `getcwd` failure disables the lexical `/`-root defense-in-depth; fail closed for recursive.
- **RM-09** LOW `rm.c:121-136` — SIGINT mid-delete still exits 0.
- **RM-10** LOW `rm_safety.c:210-251` — `rm_split_path` OOM branches `return -1` without setting errno; caller prints strerror(errno).
- Solid: fd-relative descent (openat O_NOFOLLOW|O_DIRECTORY → fstatat AT_SYMLINK_NOFOLLOW → unlinkat AT_REMOVEDIR) defeats the symlink-swap escape; symlinks never followed → no cycles; `.`/`..` exact-strcmp; no fixed-buffer path for syscalls; scrub off_t→size_t safe; `.`/`..` operand refusal handles trailing slashes.

## wc  (bin/wc/wc.c)

- **WC-01** MEDIUM `wc.c:186-195` — `-m`: a multibyte char split across a 64 KiB read boundary is miscounted (`mbrtowc` `(size_t)-2` treated as invalid: consumes lead byte, wipes mbstate). Preserve the partial tail.
- **WC-02** MEDIUM `wc.c:171,403,366` — `read()` on a directory not rejected (no fstat/S_ISDIR); emits garbage counts / doesn't error like GNU.
- **WC-03** MEDIUM `wc.c:171,248-251` — EINTR from `read()` aborts the file with a spurious error (SIGINFO handler has no SA_RESTART); the feature it exists for breaks the read. `if (n<0 && errno==EINTR) continue;`.
- **WC-04** LOW `wc.c:293,296,125-138` — `-c` and `-m` mutually exclusive; can't print both columns.
- **WC-05** LOW `wc.c:358,382-384` — `--files0-from` path silently truncated at 4095.
- **WC-06** LOW `wc.c:162-167,233` — `-w` uses byte `isspace` not `iswspace` in MB locales; dead redundant sub-expr at :163.
- **WC-07** LOW `wc.c:202,227,244` — `-L` counts code points/bytes, not display width (tabs/wcwidth); help says "display width".
- **WC-08** LOW `wc.c:390,395,404-408` — failed-to-open files still increment num_files → spurious total line.
- Solid: counters are `uint64_t`/`%ju` (no 32-bit overflow); ctype uses unsigned/`iswspace`; fixed 64 KiB buffer bounded; fd hygiene correct.

## cat  (bin/cat/)

- **CAT-01** MEDIUM `cat.c:782-810,701-736` — no same-file guard: `cat f >> f` reads its own growing output forever (disk fill/hang). fstat stdout + compare dev/ino.
- **CAT-02** LOW `cat.c:795,717` — directories not rejected explicitly (may emit nothing + exit 0).
- **CAT-03** LOW `cat.c:750-759` — cooked `fread` EOF path gates on stale `errno` for EINTR (effectively dead / mis-classify).
- **CAT-04** LOW `cat.c:876-938` — broken-pipe exit status is 0 (GNU nonzero).
- **CAT-05** LOW `cat_cooked.h:17,cat_cooked.c:88` — line-number `unsigned long` wraps at 2^32 on 32-bit (cosmetic; format matches).
- **CAT-06** LOW `cat.c:534-555,848` — `-l` (F_SETLKW) fatal EINVAL on non-regular stdout; treat as no-op.
- Solid: cooked worst-case expansion sized safe (4→out[8]); unsigned throughout for show-nonprinting; write-all short/EINTR; fd hygiene; `-B` size parse overflow guard; squeeze-blank state across chunks; per-file-open-fail continues+exit1.

## dc  (bin/dc/dc.c)  [two independent audits agreed]

- **DC-01** HIGH `dc.c:393-407` (`:` array store) — index only lower-bounded; `array_len * sizeof(bc_num*)` (int×size_t) wraps on 32-bit → tiny alloc then OOB `memset`/store; `idx+1` overflows at INT_MAX; `realloc` unchecked (NULL deref + leak); moderate index = unbounded-alloc DoS. Bound idx, size_t overflow-checked multiply, check realloc.
- **DC-02** HIGH `dc.c:439-481` (`!`) — executes arbitrary shell commands from untrusted dc input (`!/bin/sh`). Gate behind opt-in / remove.
- **DC-03** HIGH `dc.c:118-122,328-336,156-159` — unbounded macro recursion (`x`, `<r>r=r`) → C-stack overflow (`[lFx]sF lFx`). Depth cap / explicit exec stack.
- **DC-04** MEDIUM `dc.c:288-297,266` — `o` (obase) and `^` (pow) accept unbounded magnitudes → OOM/hang (`2 100000000^p`). Sane caps.
- **DC-05** MEDIUM `dc.c:264-265` — div/mod by zero relies entirely on libbc (verify no SIGFPE; guard at dc layer).
- **DC-06** MEDIUM `dc.c:193` — `[...]` initial `malloc(1024)` unchecked → NULL write.
- **DC-07** LOW `dc.c:87-93,215,239` — empty-stack `peek`/`pop` synthesize a heap zero that leaks (attacker-driven unbounded).
- **DC-08** LOW `dc.c:39,50,57,59` — assorted unchecked malloc/strdup (NULL deref on OOM).
- **DC-09** LOW `dc.c:169,173` — base parse only A–F even when ibase up to 36 (G–Z mis-parsed).
- **DC-10** LOW `dc.c:316-327` — `a` drops a stack item for empty string / zero number; `dc.c:368-376` `Z` reports base-100 limb count not digits; `dc.c:311-315` `?` reads real stdin (not current input) + truncates 1023; `dc.c:432-438` `Q` no clamp.
- Solid: register index from unsigned char into regs[256] (no OOB); ctype via unsigned char; number buffer bounded; `[...]` growth checked+NUL; `i`/`k` clamped; push growth checked.

## date  (bin/date/date.c)

- **DATE-01** MEDIUM `date.c:89,130-150` — 12-digit `CCYYMMDDhhmm` set-string parses only first 10 digits (loop caps at 5 chunks, no case 6) → silently wrong clock as root. Allow 6 chunks + case 6.
- **DATE-02** MEDIUM `date.c:161-173` — `-u` set path never applies UTC offset (both branches call `mktime` = local) → clock off by TZ offset. Use timegm-equivalent.
- **DATE-03** MEDIUM `date.c:209,212,183` — 64-bit `time_t` truncated through 32-bit `long` (`atol`/`strtol`) in `-r`/`-d @`; ERANGE unchecked. Use `strtoll`/`time_t`.
- **DATE-04** MEDIUM `date.c:152-172` — no range validation of parsed mm/dd/HH/MM before mktime; `date 13322500` normalized silently (set as root). Validate ranges.
- **DATE-05** LOW `date.c:266-269` — `strftime`==0 treated as error even for legitimately-empty output (`date +''`).
- **DATE-06** LOW `date.c:144-153` — 2-digit year has no century pivot (70→2070 not 1970).
- **DATE-07** LOW `date.c:60,77,89` — `char dot_pos` holding a string index collides with `(char)-1` sentinel (latent; make int).
- **DATE-08** LOW `date.c:209,212` — `-r abc` via `atol` → 0, no error check.
- Solid: digit validation via explicit range (no ctype UB); 2-digit split can't read past end; mktime==-1 checked; missing-arg guards for -r/-d.

## chown  (bin/chown/chown.c)  — runs as root

- **CHOWN-01** HIGH `chown.c:406,323,351,465` — fully path-based recursive descent (`lstat`+`chown`+`opendir` on reconstructed strings); classic `chown -R` symlink-swap escalation. Convert to `openat`/`fstatat`/`fchownat(AT_SYMLINK_NOFOLLOW)` fd-relative. [cross-cutting]
- **CHOWN-02** HIGH `chown.c:475-500` — `USER:GROUP`/`USER:` with a NAMED user never splits the string (`getpwnam("bob:staff")`), so every named `user:group` is rejected. Core feature broken. Split into a bounded buffer.
- **CHOWN-03** HIGH `chown.c:256-262` — any username starting with a digit is force-parsed as numeric uid (`getpwnam` never tried); a real account `4chan`/`0day` silently chowned to uid 4/0. Try getpwnam first; numeric only if whole string is digits.
- **CHOWN-04** MEDIUM `chown.c:257-261` — numeric id parse: no ERANGE check, return type `int`; `chown 5000000000` clamps to INT_MAX. strtoul into uid_t + range + found-flag.
- **CHOWN-05** MEDIUM `chown.c:475` — `USER.GROUP` (dot) separator unsupported.
- **CHOWN-06** MEDIUM `chown.c:581` — `--from=owner:group` unimplemented (errors out).
- **CHOWN-07** MEDIUM `chown.c:335-390,465` — unbounded recursion + one open DIR/level (stack/fd exhaustion).
- **CHOWN-08** MEDIUM `chown.c:446-448` — under `-R -P` (default) symlinks in the tree are neither followed nor lchowned — silently skipped (GNU lchowns them).
- **CHOWN-09** LOW `chown.c:256` — `+N` numeric-force prefix unsupported.
- **CHOWN-10** LOW `chown.c:602-603` — `-v`/`-c` are no-ops; `-c` mis-parsed as owner spec.
- **CHOWN-11** LOW `chown.c:190` — `path_join` total-size arithmetic can wrap on 32-bit size_t.
- Solid: ctype unsigned-cast; %s for paths (no format-string bug); EINTR retries; cycle dev/ino set checked-realloc; default WALK_PHYSICAL; `.`/`..` skipped; uid/gid -1 sentinel via set flags.

## chgrp  (bin/chgrp/chgrp.c)  — runs as root (shares chown design)

- **CHGRP-01** HIGH `chgrp.c:317,431,372` — path-based descent → symlink-swap escape (as CHOWN-01). fd-relative. [cross-cutting]
- **CHGRP-02** HIGH `chgrp.c:287-289` — `chown(path)`/`lchown(path)` on full path (component swap redirects the chgrp). `fchownat(dirfd,name,-1,gid,AT_SYMLINK_NOFOLLOW)`.
- **CHGRP-03** HIGH `chgrp.c:282-284` — `--reference`/`-r` set `use_reference` but not `gid_set`, so `gid` is clobbered to -1 → `chown(path,-1,-1)` no-op, exits 0. Silent security no-op. Treat use_reference as implying set gid.
- **CHGRP-04** MEDIUM `chgrp.c:251-256` — numeric group parse: endptr discarded (`"12abc"`→12), ERANGE unchecked, `(gid_t)` truncation; digit-leading names unreachable. strtoul+endptr+range, fall through to getgrnam.
- **CHGRP-05** MEDIUM `chgrp.c:349,431` — unbounded mutual recursion (stack overflow deep tree).
- **CHGRP-06** MEDIUM `chgrp.c:317-355` — one open DIR/level → fd exhaustion.
- **CHGRP-07** LOW `chgrp.c:261,256` — group resolving to real gid `(gid_t)-1` collides with error/no-change sentinel. Separate success/failure channel.
- **CHGRP-08** LOW `chgrp.c:539,549` — `-c`/`-v` unsupported; `-c` degrades to bogus "invalid group".
- **CHGRP-09** LOW `chgrp.c:216-220` — global linear-scan `visited` set → O(n²) + over-broad. Ancestor-chain only.
- Solid: %s for paths; path_join exact-malloc; visited realloc via temp; EINTR wrappers; -H/-L/-P gating; uid=-1 sentinel; getgrnam-fail errors (not gid 0).

## kill  (bin/kill/kill.c)  — numeric-only stub

- **KILL-01** HIGH `kill.c:15` — `atoi` on the signal token: `kill -KILL 4321` → signal 0 (existence probe), process survives silently. Signal-name table.
- **KILL-02** HIGH `kill.c:14-20` — `-s SIG` unimplemented; mis-parses so a subsequent `kill(0,0)` hits the whole process group; intended target never signalled.
- **KILL-03** MEDIUM `kill.c:14-24` — `-l` unimplemented (silent exit 0).
- **KILL-04** MEDIUM `kill.c:21-25` — exit status always 0 even when every `kill()` failed (POSIX requires nonzero).
- **KILL-05** MEDIUM `kill.c:14-24` — missing-operand (`kill -9`) silently no-ops, exit 0.
- **KILL-06** LOW `kill.c:15,21` — no range validation on signal number.
- **KILL-07** LOW/MEDIUM `kill.c:20` — pid via `atoi`: no overflow detection, `(pid_t)` truncation on 32-bit → may signal the wrong live pid; non-numeric operand → pid 0 (whole process group). strtol + range.
- Solid: argc<2 guard; plain `-N`/`-0`/negative-pid work; no fixed buffers.

## cut  (bin/cut/cut.c)

- **CUT-01** MEDIUM `cut.c:84-86` — unchecked position/range parse overflow (`v = v*10+...`) wraps mod 2^32 → silently wrong (sometimes 0). strtoul + overflow check. (No bitmap → no OOB; correctness/DoS-of-output only.)
- **CUT-02** LOW `cut.c:142,241-251` — read errors & directories silently ignored (no ferror, process() always returns 0) → empty output exit 0.
- **CUT-03** LOW `cut.c:100` — `cut -c-` (bare dash) accepted as whole-line instead of erroring.
- **CUT-04** LOW `cut.c:108-109` — trailing comma in list silently accepted.
- **CUT-05** LOW `cut.c:167-172,238` — empty `--output-delimiter=` + always-appended terminator minor deviations.
- Solid: no fixed bitmap (linear scan of grown ranges — the classic cut OOB is absent); length-based (embedded NUL preserved); realloc via temp; hand-rolled digit tests (no ctype UB); lo==0/lo>hi rejected; fd hygiene.

## df  (bin/df/df.c)

- **DF-01** HIGH `df.c:379-380` — `char resolved[1024]` passed to `realpath()` but PATH_MAX is 4096; a path resolving >1024 bytes overruns the stack by ~3 KB. `char resolved[PATH_MAX]`.
- **DF-02** MEDIUM `df.c:384-398` — exit status always 0 even when an operand fails (`df /nonexistent`). Track failure flag.
- **DF-03** MEDIUM `df.c:123-124` — `%511s`/`%63s` sscanf into src/tgt/typ silently truncates long mount fields → breaks longest-prefix mount matching. Size to line width.
- **DF-04** LOW `df.c:121-125` — mount line >1023 bytes split across two fgets → tail mis-parsed as a bogus mount. Detect/drain.
- **DF-05** LOW `df.c:130-132,87-108` — per-mount `unescape()` allocations never freed (leak scales with mount count).
- **DF-06** LOW `df.c:100` — octal-escape value >255 wraps in signed char (benign for real /proc/mounts).
- Solid: counters uint64_t end-to-end (no >4TB wrap); size math 64-bit; div-by-zero guarded (denom?…:0); disps alloc (nmounts+argc) sufficient; human_fmt suffix in-bounds; unescape octal bound correct; statvfs-fail → "-" cells.

## diff  (bin/diff/diff.c)

- **DIFF-01** HIGH `diff.c:220-225` — Myers trace stores a full V-vector per edit-depth → O((N+M)²) memory; two dissimilar 10k-line files ≈ 3.2 GB → OOM on 32-bit. Linear-space (divide-and-conquer) refinement.
- **DIFF-02** HIGH `diff.c:217-224` — `size_t` multiply overflow in V/trace sizing (`vsz*sizeof(int)`, `(MAX+1)*sizeof(int*)`) wraps → small buffer then OOB writes. Guard against SIZE_MAX/(2*sizeof(int)).
- **DIFF-03** HIGH `diff.c:125-134` — whole-file slurp with no cap; `cap *= 2` at 2 GB wraps to 0 → `fread` count `0-rawlen` huge → heap overflow. Cap file size + overflow-check before doubling.
- **DIFF-04** MEDIUM `diff.c:621-746` — `-r` uses `stat` (follows symlinks), recurses with no depth limit / no cycle detection → symlink-loop crash. lstat + dev/ino visited + depth cap.
- **DIFF-05** MEDIUM `diff.c:78,99,348` — `-a` compare key built from `strlen` + `fputs`; embedded NUL truncates comparison/output (reports differing lines identical). Length-tracked memcmp.
- **DIFF-06** MEDIUM `diff.c:805-838,457` — `atoi` context counts: no validation; `2*context` int overflow corrupts hunk grouping. strtol + range.
- **DIFF-07** LOW `diff.c:656` — `strdup` (dir entry) unchecked → NULL deref in qsort compare.
- **DIFF-08** LOW `diff.c:128-136` — read errors treated as EOF (no ferror) → truncated compare, wrong exit.
- **DIFF-09** LOW `diff.c:142-207` — `int` line/cap counters overflow on >2^31-line inputs; per-line individual malloc (fragmentation).
- **DIFF-10** LOW `diff.c:646-659` — `qsort(NULL,0,…)` for an empty dir (UB on some libcs).
- Solid: ctype unsigned-cast; binary compare via memcmp (NUL-safe); line split length-tracked; `join()` snprintf sized; missing-final-newline handled; xmalloc/xrealloc NULL-check.

## dirname  (bin/dirname/dirname.c)

- **DIRNAME-01** LOW `dirname.c:92-97` — output/write errors ignored; exits 0 on `dirname foo > /dev/full`. Add fflush/ferror check.
- Solid: all POSIX path edge cases correct (delegates to a verified `dirname(3)` in lib/c/src/libgen.c, strdup'd so argv untouched); NULL-checked+freed; no ctype; option parsing correct.

## dmesg  (bin/dmesg/dmesg.c)  — streams /proc/kmsg

- **DMESG-01** LOW `dmesg.c:42` — non-atomic offset streaming of the live ring buffer can tear/dup/skip lines (really a procfs-design property).
- **DMESG-02** LOW `dmesg.c:45-51` — `write()==0` would spin forever (effectively never on regular/tty fd).
- **DMESG-03** LOW `dmesg.c:59` — `close()` return unchecked (cosmetic; O_RDONLY).
- Solid: read/write drain loop correct; fixed 4 KiB stack buffer (no heap); only `n` bytes ever written; no `-s`/`-n`/`-c` parsing to overflow; not blocking (procfs returns EOF); option loop bounded.

## echo  (bin/echo/)

- **ECHO-01** LOW `echo_opts.c:54-60` — POSIXLY_CORRECT mode drops `-e`/`-E`/bundled flags/escape processing (defensible; document).
- **ECHO-02** LOW `echo_opts.c:80` — parser can never return nonzero → dead error branch in main.
- **ECHO-03** LOW `echo.c:48-52`/`echo_write.c:16-21` — broken-pipe writes emit a stderr diagnostic instead of silent SIGPIPE death; on EPIPE exit silently.
- **ECHO-04/05** LOW — 3-digit octal >0377 masked to a byte (matches GNU); `\E` accepted as ESC (benign extension). Informational.
- Solid: no over-reads (backslash-at-end + NUL-guarded lookahead); signed-char safe; no integer overflow; NO dynamic memory (streaming callback); write loop short/EINTR/EIO + emits embedded NUL; `\c` truncation correct; option parsing stops at first non-flag.

## Trivial utilities read directly (essentially clean)

- **arch** (43 lines) — clean; `--help`/`--version`, uname error-checked, `u.machine` NUL by contract.
- **true**/**false** — `return 0`/`return 1`; traditional, no options (GNU adds --help/--version but this is POSIX-acceptable).
- **chroot** (56 lines) — clean; chroot+chdir("/")+exec; execvp uses post-chroot PATH (correct). No findings.
- **clear** (65 lines) — `write_all` correct except no EINTR retry (LOW). Otherwise clean.
- **devtree** (37 lines) — reads /proc/devtree; `write(...)!=n` treats a short write as fatal rather than looping (LOW). Otherwise clean.

## awk  (bin/awk → contrib/onetrueawk, built verbatim)

Third-party One True Awk compiled verbatim from `contrib/onetrueawk/` with **no
local patches** (the project's contrib model). Findings are **document-only** for
the bin/ audit — any code fix belongs upstream / as a contrib patch series, not a
direct edit to the vendored tree. Line numbers are in `contrib/onetrueawk/`.

- **AWK-01** HIGH `run.c:1246-1266` — heap overflow in `format()` UTF-8 `%s`: the multibyte `%s` branch writes the whole string via `*p++` with NO `adjbuf` (only `format5` reserved ~51 bytes). `printf("%-s", big_utf8_string)` overflows the 24 KB awkprintf buffer. Add `adjbuf` before the copy loops. (i) genuine upstream bug.
- **AWK-02** MEDIUM `b.c:1157-1183` (`replace_repeat`) — `int` overflow in `{n}` bounded-repetition expansion (`atomlen*(firstnum-1)`); nested `a{255}{255}…` can wrap to a small malloc then memcpy past it. Use `size_t` + `overflo()`. (i) upstream.
- **AWK-03** LOW/MEDIUM `run.c:1390,1413` — `int bufsz = 3*recsize` signed overflow once records exceed ~715 MB; all record/field length math is `int` (2 GB ceiling). (i) upstream, needs huge input.
- **AWK-04** LOW `run.c:1160-1198` — **Substrate libc dependency**: integer conversions rewritten to `%jd`/`%ju` with `(intmax_t)`; if Substrate `vsnprintf` lacks the `j` length modifier, ALL awk `%d/%i/%o/%x/%u` output is garbage. (ii) VERIFY against `lib/c` printf — this is a Substrate libc fix if broken, not an awk edit.
- **AWK-05** LOW `run.c:2126-2142` — **Substrate libc dependency**: `FRAND` assumes POSIX `random()` 31-bit range (`/0x80000000`); if Substrate `random()` differs, `rand()` is biased. (ii) libc-completeness check.
- Solid: `substr`/`split`/`fldbld`/`readrec`/`indirect`/`growfldtab` bounds all correct; symtab sound; no double-free in `freefa` (xfree NULLs).

**Action for awk**: AWK-01/02/03 are upstream bugs — fix as a `contrib/onetrueawk`
patch series (not a verbatim-tree edit) or report upstream; out of scope for the
native-bin fix pass. AWK-04/05 are worth verifying against Substrate's libc
printf/`random()` and fixing there if deficient.

## Fix plan / priority

1. Shared **fd-relative descent** conversion (fixes CHOWN-01/07/08, CHGRP-01/02/05/06,
   CP-08, MV-01/05 to the extent applicable), modeled on rm. Biggest security win.
2. Broken-feature HIGH/MEDIUM: CHOWN-02/03/04/05, CHGRP-03/04, KILL-01/02/04/07,
   CP-01/02, MV-02, DC-01/02/03, DF-01, DIFF-02/03.
3. DoS/correctness MEDIUM: recursion caps, cut/diff/date overflow parses, wc/cat
   directory+EINTR handling, mv chmod/chown order + backup suffix.
4. LOW batches per utility.

Verification: ASan/UBSan host builds + crafted repros + behavior checks, one
finding per commit, mirroring the bin/sh audit.

---

# Batch 2 — 2026-07 (further ~49 utilities)

Second parallel audit wave. Same threat model / 32-bit notes. A recurring
finding across this batch: **many programs discard write/`fclose` errors and
`return 0`**, and a surprising number are **unimplemented stubs or thin
wrappers**. New cross-cutting theme for the privileged/tty tools (`wall`,
`write`, `who`, `w`): **untrusted `utmp` `ut_host`/`ut_line` and message
bodies are emitted to terminals with no control-character sanitization →
terminal-escape injection**, and `ut_line` is used to build `/dev/<line>`
paths without `/`-rejection or `O_NOFOLLOW`.

## kill  (bin/kill/) — FIXED (commit)
- KILL-01..07 all fixed by a rewrite: real signal name<->number table,
  `-s`/`-l`, strtol-parsed pids with range checks (no silent pid 0), correct
  nonzero exit status. (Verified ASan; committed.)

## passwd  (bin/passwd/passwd.c) — SETUID, privilege-critical
- **PASSWD-01** HIGH `passwd.c:103-171` — no `pwdb_lock`/flock on /etc/shadow; concurrent passwd/useradd race → lost-update / shadow DB corruption; read-for-auth then read-for-rewrite with no lock = TOCTOU.
- **PASSWD-02** HIGH `passwd.c:140-166` — fwrite/fputs/fclose returns unchecked; a write error (ENOSPC) makes a truncated shadow.new get renamed over /etc/shadow → accounts lose hashes / box unloginnable. fsync + check + unlink-on-error.
- **PASSWD-03** HIGH `passwd.c:118,166` — temp `/etc/shadow.new` created 0644 (world-readable) holding all hashes; chmod 0640 only after rename and unchecked. Create O_EXCL 0600 / umask 077, fchmod before rename.
- **PASSWD-04** MEDIUM `passwd.c:277-297` — salt folded through a 32-bit LCG (≤32 bits entropy); /dev/urandom-fail path derives salt from pid*const+uid (predictable). Fill salt directly from urandom, fail closed.
- **PASSWD-05** MEDIUM `passwd.c:43-60` — echo-off termios not restored on SIGINT → leaks next typed line, breaks terminal. Signal handler / block signals.
- **PASSWD-06** MEDIUM `passwd.c:204-309` — cleartext passwords + stored hash never scrubbed (explicit_bzero) from stack.
- **PASSWD-07** LOW `passwd.c:50-63` — read_password returns unsigned length; `<0` error checks are dead; EOF becomes an empty password.
- **PASSWD-08/09/10** LOW — short urandom read leaves salt bytes uninit; shadow line >511 mis-parsed by fgets(512); empty stored password skips non-root re-auth.
- Solid: privilege gate (non-root can only target self + must re-auth) correct; %s for username (no format-string); read_password bounds-checked+NUL.

## su  (bin/su/su.c) — SETUID, privilege-critical
- **SU-01** HIGH `su.c:172-197` — environment NEVER sanitized across the privilege boundary; `LD_PRELOAD=/tmp/evil.so su -c cmd` (or IFS/ENV/BASH_ENV) runs as the target uid → arbitrary code execution. Whitelist env, strip LD_*/IFS/etc.
- **SU-02** HIGH `su.c:166` — `initgroups()` return discarded; on failure the switched shell keeps root's supplementary groups (gid 0/wheel). Check it.
- **SU-03** MEDIUM `su.c:172-183` — PATH not reset for the target/login shell (inherits caller PATH). Set a secure PATH for uid 0 / login.
- **SU-04** MEDIUM `su.c:96-98,152-155` — empty stored hash / missing shadow entry falls through to a no-password success (combines with SU-08 EOF-empty-password). Treat missing shadow as failure.
- **SU-05..09** LOW/MED — password buffer not cleared; non-constant-time strcmp; echo-off not restored on signal; read_password `<0` dead (EOF→empty pw); login argv[0] lacks leading '-'.
- Solid: priv-drop ORDER correct (setgid→initgroups→setuid); root skips prompt; failed auth returns before any setuid; setgid/setuid returns checked; locked accounts (*/!) rejected; %s formats.

## sed  (bin/sed/) — runs untrusted scripts
- **SED-01** HIGH `sed_parse.c:295-306,349,576` — `-S` sandbox doesn't stop `w`/`W`: files are `fopen("w")` (truncated) at PARSE time; `sed -S -e 'w /etc/motd' /dev/null` truncates it. Defer fopen to first write, skip when sandboxed.
- **SED-02** HIGH `sed_exec.c:702,662,686` — default (non-`-S`) execution of an untrusted script is RCE + arbitrary file R/W (`e cmd`, `s///e`, `r`, `w`). Document / require opt-in.
- **SED-03** HIGH `sed.c:124-156` — `-i`: write/fclose errors never checked → truncated temp renamed over original (data loss). Check fflush+ferror+fclose, unlink temp on error.
- **SED-04** MEDIUM `sed.c:93-104,151` — `-i` doesn't preserve mode/owner; non-mkstemp fallback uses a predictable `name.sed<pid>` opened `fopen("w")` → symlink TOCTOU (arbitrary write). fstat+fchmod/fchown; O_CREAT|O_EXCL only.
- **SED-05** MEDIUM `sed_util.c:60-73` — `db_reserve` doubling overflows 32-bit size_t (`newcap*=2` → 0 hang; `need` wrap → undersized realloc then memcpy overflow). Overflow-check.
- **SED-06** MEDIUM `sed_exec.c:122-142,270` — regex recompiled per match iteration; untrusted `s///`/address regex = ReDoS/CPU DoS. Compile once + match budget.
- **SED-07..11** LOW — caps[] not zero-init before regex_match; `-i` fflush after dup2 (wrong file); leftover temp on fdopen fail; no branch-loop guard; out-of-range `\N` silently ignored.
- Solid: ctype unsigned-cast; y/// length-mismatch rejected; brace nesting depth-capped(64)+iterative exec; realloc-via-temp; length-tracked lines (embedded NUL safe); labels validated.

## sort  (bin/sort/sort.c)
- **SORT-01** MEDIUM `sort.c:158,258,352` — comparators use strcmp/str* (NUL-terminated) on length-tracked lines → embedded NUL truncates keys; `-u` silently drops distinct binary lines. memcmp over min(len).
- **SORT-02** MEDIUM `sort.c:346-350` — `-s` (stable) breaks `-u`: the uniqueness comparator leaves `stable=1` so equal keys never compare 0; `sort -su` emits dupes. Set neq.stable=0.
- **SORT-03** MEDIUM `sort.c:100-133` — no external/temp merge (comment claims one); whole input in RAM → OOM DoS on 32-bit. Document or implement.
- **SORT-04** MEDIUM `sort.c:399-407` — `-k`/`-t` field keys parsed then IGNORED; `sort -t: -k3` silently whole-line sorts (exit 0). Implement or reject.
- **SORT-05/06** LOW — `parse_int_key`/`version_compare` numeric accumulation overflow (signed UB / wrap). strtoll+saturate.
- **SORT-07** LOW `sort.c:85-86` — `cap_lines*2`/`nc*sizeof` 32-bit multiply overflow (unreachable-in-practice). Guard `nc > SIZE_MAX/sizeof`.
- **SORT-08..10** LOW — empty `-t ''` sets NUL separator; `-c` fixed `-` filename label + no `-u` strictness; `-R` hash comparator not a strict total order (drops collisions under `-u`).
- Solid: `-o` reads all input before opening output (self-sort safe); qsort_r convention matches libc; unsigned-char in slow paths; alloc checks; stability via orig_index.

## tar  (bin/tar/tar.c) — extraction of untrusted archives = HIGH surface
- **TAR-01** HIGH `tar.c:444-455,485,599-605` — symlink-directory traversal: only the final component gets O_NOFOLLOW; a symlink member `evil -> /etc` then a `evil/passwd` member writes through it. Symlink target not sanitized either. openat(O_NOFOLLOW|O_DIRECTORY) per component from a pinned root; reject abs/`..` link targets.
- **TAR-02** HIGH `tar.c:606-611,618,620` — hardlink source used verbatim; a member `linkname=/etc/shadow mode=0777` makes the following chmod/lchown rewrite the victim inode (root priv-esc). Confine link targets to the extraction root.
- **TAR-03** HIGH `tar.c:511-517` — `pax_parse` integer underflow: `rec` trusted ≥ prefix len; a record like `"1 =\n"` → `n = 1-2 = SIZE_MAX` → `memchr`/`strndup` huge over-read (crash/infoleak). Require rec ≥ prefix+2.
- **TAR-04** MEDIUM `tar.c:565,602,321` — name/prefix/linkname[100/155/100] consumed as C strings without guaranteed NUL → read past field / stack. memcpy+NUL or strnlen.
- **TAR-05** MEDIUM `tar.c:618-624` — untrusted setuid/setgid restored (`chmod & 07777`) and ownership applied by default (`no_same_owner` defaults false) → setuid-root binary from a hostile archive. Strip setuid/setgid; default same-owner off.
- **TAR-06** MEDIUM `tar.c:569-571` — `malloc((size_t)hsize)` unchecked + 32-bit truncation of a 64-bit attacker hsize (OOM / malloc(0) desync). Cap pax header size.
- **TAR-07** LOW `tar.c:458,469` — dead absolute-path clause; `oct2i` skips non-octal (GNU base-256 fields desync); chmod on a symlink member follows it.
- Solid: textual `../`/`/..` rejection; leading `/` stripped; off_t used for member data (no 32-bit truncation); checksum via unsigned char; create-side lstat (no symlink recursion).

## xargs  (bin/xargs/)
- **XARGS-01** HIGH `xargs.c:106,123` — `-I` replacement buffer sizing `(vl-rl)*(cap/rl+1)` overflows 32-bit size_t → tiny malloc then unguarded literal-byte copy overruns it. Overflow-check out_cap + guard the else branch.
- **XARGS-02/03** MEDIUM `xargs.c:296,273` — `-I` line buffer unbounded (ignores -s/ARG_MAX); `-s` budget omits sizeof(char*) overhead + no ARG_MAX clamp → silent E2BIG.
- **XARGS-04..07** LOW — unterminated quote silently accepted (swallows delims/newlines); `-d ''`/`-d '\x'` → NUL/0 silently; `-a` files never closed; `-P0` serial not unlimited.
- Solid: no ctype (unsigned-char getc); replace_tok no infinite recursion; argv NULL-terminated; buf_putc growth + match-path realloc guard correct; exit-status mapping correct.

## tail  (bin/tail/)
- **TAIL-01** HIGH `tail_io.c:179-194` — `tail -c 4G` from a pipe: `count` (int64) truncates to `size_t` 0 → `malloc(0)`, `ring[head]` OOB write, `(x)%0` SIGFPE. Reject/clamp count>SIZE_MAX, guard cap!=0.
- **TAIL-02** MEDIUM `tail_parse.c:118-143` — documented `-n -N`/`-c -N` (explicit minus, from-end) prints nothing (leading `-` not handled → negative count → "output nothing"). Skip optional leading `-`.
- **TAIL-03** MEDIUM `tail_io.c:216-284` — pipe line-mode + reverse-mode buffer the ENTIRE input into RAM → OOM on a large/infinite producer. Bounded ring of last-N offsets.
- **TAIL-04** MEDIUM `tail_main.c:172` — a failed open aborts ALL follow (`-F missing` never waits; `-f present absent` refuses to follow present). Don't gate follow on exit_status.
- **TAIL-05..10** LOW — rotation drops old-fd tail + unchecked fstat; backward block read treats EINTR/short as fatal; `-b +N` unchecked *512 overflow + `-s 0` busy-poll; directory operand not detected; INT64_MIN range-check UB; first follow header missing separating newline.
- Solid: write_all short/EINTR; positive-side overflow guards; seekable paths 64-bit off_t with underflow clamp; pipe *byte* ring bounded; --pid array bounded.

## printf  (bin/printf/printf.c)
- **PRINTF-01** HIGH `printf.c:96-130` — infinite loop when FORMAT consumes no arg but args remain (`printf 'hello' x` hangs). Break when argi didn't advance.
- **PRINTF-02** HIGH `printf.c:111-119` — 1-byte stack OOB: the `.` and conversion-char writes to `spec[32]` aren't bounds-guarded (only the flag/width loops are). Guard every write.
- **PRINTF-03** MEDIUM `printf.c:60,65,73` — width/precision copied verbatim into `spec` → `printf '%999999999d'` huge-pad DoS. Parse+clamp.
- **PRINTF-04** LOW-MED `printf.c:60,65` — numeric conversions ignore ERANGE/endptr; `'A` char-code form unimplemented.
- **PRINTF-05/06** LOW — `*` width, `%b`, `%f`, `\xHH`, `\c` unimplemented; misc `%%`-with-flags/`%c` signed/missing-arg + unchecked writes.
- Solid: `%n` can't reach real printf (validated conversion set); missing %s/%d args default safely; interp_escapes output ≤ input, octal capped, trailing-`\` handled.

## touch  (bin/touch/touch.c)
- **TOUCH-01** HIGH `touch.c:89-95` — `-t` fields used uninitialized when sscanf doesn't match all (`touch -t 1a2b3c4d f`) → UB / garbage timestamp. Check sscanf return / init.
- **TOUCH-02** MEDIUM `touch.c:84-96,53-67` — no range validation of mm/dd/hh/mm before the hand-rolled epoch math (`touch -t 13322500 f`). Validate.
- **TOUCH-03** MEDIUM `touch.c:153-157` — directories can't be touched (open O_WRONLY → EISDIR treated fatal). Handle EISDIR, fall through to utimensat.
- **TOUCH-04..10** LOW — dead EEXIST branch; iso[24] truncation on wild fields; `-h` doesn't suppress creation / no O_NOFOLLOW; `-r` drops sub-second + follows link under `-h`; `-t` treated UTC not local; `ss` unvalidated; -d/-r/-t precedence by hardcoded order.
- Solid: `-d @epoch` strtoll+endptr (64-bit time_t safe); parse_t buffer bounds-checked; -r stat checked; multi-operand exit status; -a/-m UTIME_OMIT correct.

## tee  (bin/tee/tee.c)
- **TEE-01** HIGH `tee.c:24` — raw `write(1,...)` ignores short writes/EINTR → silent data loss. Loop + retry EINTR.
- **TEE-02** HIGH `tee.c:18` — `fopen` failure undiagnosed, exit stays 0 (POSIX violation). Check + perror + status 1.
- **TEE-03** HIGH `tee.c:7,17` — fixed `files[10]` silently drops the 10th+ operand. malloc(argc).
- **TEE-04..07** MED — read EINTR truncates input; fwrite/fclose returns unchecked; always `return 0`.
- **TEE-08/09** MED/LOW — `-i`/`-p`/`--`/long-opts unimplemented (`-i` becomes a filename, no SIGINT ignore); fopen follows symlinks (no O_NOFOLLOW/mode control).
- Solid: length-based writes (embedded NUL safe); NULL-guards on failed opens (no crash/fd-leak).

## uniq  (bin/uniq/uniq.c)  + paste + sum
- **UNIQ-01/PASTE-01** MEDIUM `uniq.c:123`, `paste.c:82` — line-buffer `cap*2` 32-bit overflow → wrap to 0 → heap overflow on a ≥2 GiB line. Overflow-check.
- **UNIQ-02** MEDIUM `uniq.c:100-110` — str*cmp truncates at embedded NUL → wrong dedup on binary input. memcmp over min(len).
- **UNIQ-03** MEDIUM `uniq.c:148,288` — output write/fclose errors ignored → data loss, exit 0.
- **UNIQ-04/05/06/07** LOW — read errors/OOM treated as EOF; `-f/-s/-w` unvalidated atoi; `-c` int overflow.
- **PASTE-02..03** LOW — read error == EOF (no ferror); FILE* leak on later open failure.
- Solid (paste): delimiter cycling bounded; length-tracked; final fflush checks. **Solid (sum): no findings** — BSD/SysV checksums correct, uint64 counts, EINTR/short-read handled, unsigned-char, fds closed.

## pwd / which / whoami
- **PWD-01** HIGH `pwd.c:9-12` — returns exit 0 even when getcwd fails. **PWD-02** HIGH `pwd.c:10` — error printed to stdout (`cd $(pwd)` into "pwd: error"). **PWD-03/04/05** MED/LOW — 1024-byte buffer < PATH_MAX (ERANGE on deep cwd); `-L`/`-P` ignored; unchecked write.
- **WHICH-01/02** LOW — getcwd-fail silently disables --show-dot; not-found dumps raw $PATH. Solid otherwise (exact-sized buffers, PATH strdup'd, freed).
- **WHOAMI-01/02** LOW — pw_name NULL not guarded (puts(NULL) UB); unknown option → "extra operand". Solid: getpwuid(geteuid()) NULL checked, %s-safe, write checked.

## w / who / uptime
- **WHO-01/W-01** MEDIUM `who.c:55`, `w.c:188` — untrusted utmp `ut_host` (set by telnetd from the network peer) printed raw → terminal-escape injection into the operator's terminal. Filter non-printing bytes to `?`.
- **UPTIME-01** LOW `uptime.c:44-47` — `count_users()` is a hardcoded `return 1` stub (always "1 user"); header comment lies.
- **W-02..04, WHO-02/03** LOW/info — find_what() O(users×procs) /proc rescan; manual /proc/uptime parse; only exact `-b`/`-H`; no diagnostic on unreadable utmp.
- Solid: `field()` helper memcpy's exactly UT_*SIZE into [SIZE+1]+NUL (no OOB); all buffers snprintf-bounded; localtime NULL-checked; %s-safe; fd/FILE closed.

## nice / renice
- **NICE-01** HIGH(correctness) `nice.c:20` — `setpriority` is COMMENTED OUT; `nice` is a silent no-op (`inc` parsed then discarded). Uncomment+check.
- **NICE-02** HIGH `nice.c:9-12` — two-token `-n adj` form execs the numeric arg as the command (`nice -n 5 ls` runs "5"). Parse argv[2] when argv[1]=="-n".
- **NICE-03/04** MED/LOW — wrong exit codes (should be 125/126/127; msgs to stdout); atoi adj no overflow/clamp.
- **RENICE-01** MEDIUM `renice.c:67,98` — strtol overflow unchecked; a huge pid/uid truncates to an in-range id → renices the WRONG process/user. errno/ERANGE + range check.
- **RENICE-02/03** LOW — trailing/empty `-p`/`-u` selector is a silent no-op exit 0; numeric id accepts negative via (id_t) wrap.
- Solid (renice): getpwnam NULL-checked; getpriority -1 disambiguated; setpriority checked per-target; priority clamped; %ld formats.

## wall / write
- **WALL-01** HIGH `wall.c:91` — message written raw to every tty; no control-char sanitization → terminal-escape injection (retitle/clear/DECRQSS type-back/OSC52) to all users incl. root console. carefulputc-style escape.
- **WALL-03** MEDIUM `wall.c:83-88` — `ut_line` only NUL-clamped, not validated; `ut_line="../../etc/passwd"` → open("/dev//../../etc/passwd") writes the body into an arbitrary file. Reject `/` and leading `.`.
- **WALL-04** MEDIUM `wall.c:88` — open without O_NOFOLLOW / no isatty gate → symlink target redirection. Add O_NOFOLLOW + isatty check.
- **WALL-02/05** LOW — read_message `cap-1` underflows if cap==0 + silent truncation; no privilege scoping / per-write result check.
- **WRITE-01** HIGH(stub) `write.c` — 13-line stub: no utmp/tty/mesg/sanitization, hardcodes sender as "root" (spoofing). All security logic is a latent requirement.
- Solid (wall): utmp filtered USER_PROCESS; ut_line NUL-terminated (no OOB read); fds closed.

## Stubs / thin wrappers (no real attack surface as-is)
- **od** — 25-line stub; `argv[1]` used verbatim as path (every flag → fopen fail); `offset` is 32-bit `long` (wraps at 2 GiB); read-error==success. Unimplemented.
- **test** — 28-line stub; only `-f`/`-d`/`-e`; no `[`/`]`, no parser, no int/string ops; errors return 1 not 2. Unimplemented.
- **split** — 10-line stub (`split: not implemented`, exit 0). Unimplemented.
- **pgrep** — 9-line stub (`printf` only, exit 0). Unimplemented.
- **newgrp** — 12-line stub (prints "not fully implemented", exit 0; commented-out ref logic is itself unsafe). Unimplemented.
- **prof** — 6-line stub. **size** — stub (`argv[1]` never opened). **sync** — stub (global sync(); `-f`/`-d`/operands ignored). **nproc** — stub (always prints `1`; wrong on SMP → serializes `make -j$(nproc)`). **tc** — 6-line traffic-control stub. **tabs** — 7-line stub emitting `ESC c` (full RIS reset, not tab clear).
- **yes** — repeats only argv[1] (not argv[1..]); no EPIPE/write-error handling (busy-spin if SIGPIPE ignored).
- **vi** — clean 7-line wrapper → `exvi_main` (library link, no exec/path/argv hazard). Editor surface lives in `usr.lib/exvi/` (NOT audited here).
- **umount** — clean 37-line `umount2(2)` wrapper (no -a/-l/mtab); minor argv[0] NULL-deref nit. **tty** — clean (ttyname NULL handled, POSIX exit codes). **uname** — sound (only `-a` omits `-o`, and memset-before-uname hardening nit). **reset** — clean (write_all, tcgetattr checked, sane termios). **ps**/**top** — well-built (kernel strlcpy's comm so no /proc parenthesized-comm bug; findings mostly LOW: type-pun cmdline read, terminal-escape of comm in top, ~512 KB top stack frame). **tr** — hardened OpenBSD tr (unsigned-char indexing safe; LOW: unchecked writes, `[c*n]` count int-truncation). **readlink** — sound (readlink NUL-terminated, ELOOP bounded; LOW: snprintf-truncation defeats ENAMETOOLONG guard). **time** — memory-safe (LOW: `-c`/`-t`/`-f` custom format emit nothing).

## ping  (bin/ping/ping.c) — raw socket, likely setuid-root
- **PING-01** CRITICAL `ping.c:264,316` — raw SOCK_RAW opened but privilege is NEVER dropped; the whole reply-parse loop consumes attacker packets as root. `setgid(getgid())`+`setuid(getuid())` right after socket().
- **PING-02/03** LOW — `-s`/`-c`/`-i` via atoi/atol/strtod: negative/overflow unvalidated (`-s -4` wraps guard; `-c -5` → unlimited; `-i 0` → busy flood as root). strtol+range.
- **PING-04** info — `-t ttl` parsed but never applied (setsockopt stub).
- Solid: v4/v6 reply parsing is carefully bounds-checked (ihl validated before indexing; timestamp copy-out length-checked; short packets rejected); send buffer never overruns pkt[1500]; stats div-by-zero guarded; uint8_t fields; no format-string.

## rmdir  (bin/rmdir/)
- **RMDIR-01** HIGH `rmdir_parents.c:237-239` — `-p` leaves the top component of an absolute path (`rmdir -p /a/b/c` removes c,b but not a) and exits 0; inconsistent with `//a/b`. Only break when the path is exactly `/`.
- **RMDIR-02** LOW `rmdir_opts.c:39` — leading `+` in optstring disables permutation (`rmdir dir -p` treats `-p` as a filename).
- Solid: truncation loop always terminates; `/`//`//`/`.`/`..` protected; malloc'd paths + NUL; --ignore-fail-on-non-empty correct; fstatat AT_SYMLINK_NOFOLLOW; per-operand exit status.

## Fix priority (batch 2)
1. CRITICAL/privilege: PING-01 (drop priv), SU-01/02 (env sanitize + initgroups), PASSWD-01/02/03 (lock + checked writes + temp perms).
2. HIGH memory-safety: PRINTF-01/02, TAIL-01, XARGS-01, TAR-01/02/03, SED-01/03/05, TEE-01/02/03, UNIQ-01/PASTE-01.
3. HIGH correctness: NICE-01/02, RMDIR-01, PWD-01/02, KILL (done).
4. Terminal-escape injection: WALL-01, WHO-01/W-01 (+ ut_line traversal WALL-03/04).
5. Stubs (test/od/split/pgrep/nproc/sync/size/prof/tc/tabs/write/newgrp): implement or make honestly fail; out of scope for a bug-fix pass.
6. awk (contrib): AWK-01/02/03 upstream — contrib patch, not a verbatim-tree edit.
