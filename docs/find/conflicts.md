# `find` Conflict Resolution Policy

This document records every known semantic conflict among the target
dialects (POSIX, FreeBSD, OpenBSD, NetBSD, GNU) and the resolution
adopted by this implementation.

## Governing principle

**FreeBSD is the default BSD baseline.**  When a shared GNU/BSD feature
conflicts semantically the FreeBSD-compatible behavior wins.  GNU-only
and other-BSD behaviors are available as overlays or accepted silently
where they do not alter observable output.

---

## C01 — Regex flavour default

| Dialect     | Default | Notes |
|-------------|---------|-------|
| POSIX       | BRE     | `-regex` unspecified in POSIX |
| FreeBSD     | BRE     | `-E` switches to ERE |
| OpenBSD     | BRE     | No `-E`; only BRE |
| NetBSD      | BRE     | `-E` switches to ERE |
| GNU         | Emacs   | `-regextype posix-extended` for ERE |

**Resolution:** Default is **BRE** (FreeBSD).  `-E` switches to ERE.
`-regextype` is accepted as a GNU extension and overrides the default;
`posix-basic` and `sed` select BRE, `posix-extended`, `egrep`, and
`posix-egrep` select ERE.  Emacs regex is not supported.

---

## C02 — `-perm` prefix syntax

| Syntax    | Meaning              | Supported by |
|-----------|----------------------|-------------|
| `mode`    | Exact match          | POSIX, all  |
| `-mode`   | All bits set         | POSIX, all  |
| `/mode`   | Any bit set          | FreeBSD, GNU |
| `+mode`   | Any bit set (legacy) | GNU (deprecated) |

**Resolution:** All four forms are accepted.  `+mode` is treated as a
legacy alias for `/mode` (any-bit match) for GNU compatibility.
POSIX only requires the first two; the other two are extensions.

---

## C03 — `-newerXY` invocation form

| Dialect | Form                          |
|---------|-------------------------------|
| FreeBSD | `-newer XY file` (3 tokens)   |
| GNU     | `-newerXY file` (2 tokens)    |

**Resolution:** Accepted as **`-newerXY file`** (GNU form, 2 tokens).
The FreeBSD 3-token form (`-newer XY file`) is not supported directly;
instead the plain `-newer file` works as the traditional mtime
comparison and `-newerXY file` handles all cross-timestamp cases.

---

## C04 — `-execdir {} +` batching

| Dialect  | Supported | Notes |
|----------|-----------|-------|
| FreeBSD  | Yes       | |
| OpenBSD  | No        | Docs explicitly omit it |
| NetBSD   | No        | |
| GNU      | Yes       | |

**Resolution:** `-execdir {} +` is supported (FreeBSD/GNU behavior).
No strict compatibility mode exists to disable it.

---

## C05 — Implicit `-print` inhibitor set

All dialects agree: any action present in the expression tree inhibits
the implicit `-print`.  The inhibitor set includes:

`-print`, `-print0`, `-printx`, `-ls`, `-printf`, `-fprint`, `-fprint0`,
`-fls`, `-fprintf`, `-exec`, `-execdir`, `-ok`, `-okdir`, `-delete`, `-quit`

There is no known dialect disagreement on this set.

---

## C06 — `-delete` and `-L` (follow) interaction

| Dialect  | Behaviour |
|----------|-----------|
| FreeBSD  | No rejection; `-delete` operates on the referent |
| GNU      | No rejection; `-delete` operates on the referent |

**Resolution:** `-delete` combined with `-L` (follow mode) is rejected
at startup with a diagnostic.  Silently deleting symlink referents is a
safety hazard; users must use explicit `-P` or omit `-L`.

---

## C07 — `-follow` as global modifier vs. predicate

| Dialect  | Behaviour |
|----------|-----------|
| FreeBSD  | Global modifier (changes traversal mode regardless of position) |
| OpenBSD  | Global modifier |
| GNU      | Deprecated positional option; treated as global |

**Resolution:** `-follow` in the expression sets `g_deref = DEREF_ALWAYS`
globally.  It is modelled as a global traversal modifier (returns TRUE
in the AST) rather than as a short-circuitable predicate.  Same for
`-depth`, `-xdev`, `-maxdepth`, `-mindepth`, `-daystart`.

---

## C08 — `-depth` semantics

| Dialect  | Behaviour |
|----------|-----------|
| BSD `-d` | Alias for `-depth` |
| GNU      | `-depth` is a global option |

**Resolution:** Both `-d` and `-depth` accepted.  Both set global
post-order traversal.  Neither is modelled as a predicate.

---

## C09 — `-ok` locale-dependent affirmative response

| Dialect  | Affirmative |
|----------|-------------|
| POSIX    | LC_MESSAGES `yesexpr` |
| GNU      | `y` or `Y` prefix |
| BSD      | `y` or `Y` prefix |

**Resolution:** `y` or `Y` prefix is accepted.  Full locale `yesexpr`
matching is not implemented.

---

## C10 — `-exec {} +` batch size limits

| Dialect  | Limit |
|----------|-------|
| FreeBSD  | 5,000 pathnames |
| GNU      | ARG_MAX-based |

**Resolution:** Batching uses `ARG_MAX / 2` as the byte-size threshold
(GNU approach) **and** caps at 5,000 pathnames (FreeBSD approach).
Both limits are enforced; whichever is hit first triggers a flush.

---

## C11 — `-execdir` PATH safety

| Dialect  | Behaviour |
|----------|-----------|
| GNU      | Rejects if `.` or empty component in PATH |
| FreeBSD  | No restriction |

**Resolution:** PATH safety check is enforced per GNU behaviour
(REQ-FIND-102).  `.` or empty PATH components cause a diagnostic
and failure when `-execdir` is used.

---

## C12 — Optimizer default level

| Dialect  | Default |
|----------|---------|
| GNU      | `-O1` (name tests first) |

**Resolution:** Default is **`-O1`** (moves pure name tests before
stat-requiring tests in AND chains).  `-O0` disables all reordering.
The optimizer never reorders side-effecting actions.
