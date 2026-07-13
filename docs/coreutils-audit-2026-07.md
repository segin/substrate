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
