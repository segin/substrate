# Kernel `O_NOFOLLOW` / `O_DIRECTORY` enforcement gap (2026-07)

## Finding

Substrate's kernel `open()` path (`kern_open_from()` in `sys/kern/syscall.c`)
resolves paths via `vfs_perso_lookup()`, which **always follows a
final-component symlink**, and it **never inspects the `O_NOFOLLOW` or
`O_DIRECTORY` open flags**. A repo-wide search finds `O_NOFOLLOW` referenced
only in the constant definition and the FreeBSD-personality flag translation —
nowhere in the VFS open path.

Consequence: `open(path, ... | O_NOFOLLOW)` silently follows a symlink, and
`open(path, ... | O_DIRECTORY)` silently opens a non-directory.

## Impact

This undermines the symlink-swap (TOCTOU) hardening already landed in the
coreutils audit, which relies on the kernel honoring `O_NOFOLLOW`:

- **tar** (CU-TAR-01) — pinned-root extraction opens each path component with
  `openat(O_DIRECTORY | O_NOFOLLOW)`. On the host (where the fix was verified)
  this blocks a symlink-dir traversal escape; **on Substrate the flag is
  ignored, so the escape is not actually prevented.**
- **cp** (CU-CP) and **mv** (CU-MV) — the `O_NOFOLLOW` dest guards likewise are
  not enforced on-target.
- **chown / chgrp `-R`** (CU-CHOWN-01/07/08, CU-CHGRP-01/02/05/06) — the audit's
  "biggest security win" is to convert the path-based `-R` descent onto the
  fd-relative `openat(O_NOFOLLOW|O_DIRECTORY)` + `fstatat` + `fchownat` model
  (as `rm` already does). That conversion is **pointless until the kernel
  enforces `O_NOFOLLOW`** — a fd-relative walk that still follows a swapped-in
  symlink provides no protection. This is why those three findings remain
  deferred.

## Proposed fix

`docs/design/kernel-o_nofollow-enforcement.patch` (in this directory) adds two
checks to `kern_open_from()`, gated behind the respective flags so normal opens
are unaffected:

1. **`O_NOFOLLOW`** (and not `O_CREAT`): re-resolve the parent via
   `kern_resolve_parent_at()` and `finddir()` the final name to inspect the
   *unfollowed* entry; if it is `FS_SYMLINK`, return `-ELOOP`.
2. **`O_DIRECTORY`**: if the resolved target is not `FS_DIRECTORY`, return
   `-ENOTDIR`.

The patch builds clean (`make -C sys`, `-Werror`). It is **not yet committed**
because it modifies the kernel's core `open()` path and must be boot-verified
in a working Substrate boot environment first — the local `qemu -kernel`
images in the dev checkout do not currently reach userland (the *unmodified*
kernel panics identically with "No init found"), so an end-to-end boot +
symlink-swap repro could not be completed here.

## Verification plan (once a bootable image is available)

1. Boot the patched kernel; confirm it reaches login (proves the common
   open path — init, mounts, exec, shell — is unaffected).
2. Repro: `ln -s /etc target; open("target", O_DIRECTORY|O_NOFOLLOW)` must
   return `ELOOP`; a real directory must still open.
3. Re-run the tar symlink-dir extraction repro on-target and confirm the
   victim file is untouched.
4. Then land the chown/chgrp fd-relative `-R` descent rewrite on top.
