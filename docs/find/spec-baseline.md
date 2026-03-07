# `find` Specification Baseline

## Dialect precedence

**Default: FreeBSD.** When a shared GNU/BSD feature conflicts semantically,
FreeBSD-compatible behavior wins. OpenBSD/NetBSD and GNU behaviors are
available as overlays.

## POSIX.1-2024 core (mandatory)

| Feature | Status |
|---------|--------|
| Recursive descent | ✅ |
| Boolean expression (`()`, `!`, AND, OR) | ✅ |
| Arbitrary depth, no path-length failures | ✅ |
| Ancestor loop detection + diagnostic | ✅ |
| `-name`, `-path`, `-type`, `-perm` | ✅ |
| `-user`, `-group`, `-links`, `-size` | ✅ |
| `-newer`, `-atime`, `-mtime`, `-ctime` | ✅ |
| `-exec {} ;`, `-exec {} +`, `-ok` | ✅ |
| `-print`, `-prune` | ✅ |
| `-H`, `-L` | ✅ |
| `-iname` (POSIX Issue 8) | ✅ |
| Implicit `-print` when no action present | ✅ |
| Default starting point `.` when none given | ✅ |

## FreeBSD-default extensions

| Feature | Status | Notes |
|---------|--------|-------|
| `-d`/`-depth` | ✅ | Post-order traversal |
| `-x`/`-xdev`/`-mount` | ✅ | No cross-device descent |
| `-E` (ERE) | ✅ | Switches regex to ERE |
| `-f path` | ✅ | Explicit path argument |
| `-s` (sorted) | ✅ | Lexicographic readdir |
| `-delete` | ✅ | Implies `-depth` |
| `-empty` | ✅ | Regular files + dirs only |
| `-execdir` | ✅ | |
| `-inum` | ✅ | |
| `-ls` | ✅ | BSD ls-style output |
| `-print0` | ✅ | NUL-terminated |
| `-regex`/`-iregex` | ✅ | BRE default, ERE with `-E` |
| `-samefile` | ✅ | dev+ino match |
| `-not`, `-and`, `-or` | ✅ | Operator aliases |
| `-maxdepth`/`-mindepth` | ✅ | |
| `-B*` birthtime | ❌ | Platform-dependent |
| `-acl`, `-flags` | ❌ | Platform-dependent |
| `-sparse` | ❌ | Platform-dependent |

## OpenBSD deltas

| Feature | Status | Notes |
|---------|--------|-------|
| `-X` (safe xargs output) | ✅ | Accepted |
| `-h` → `-L` alias | ✅ | |

## NetBSD deltas

| Feature | Status | Notes |
|---------|--------|-------|
| `-printx` | ✅ | Backslash-escaped output |
| `-quit` | ✅ | Immediate exit |
| `-rm` | ❌ | Alias for `-delete` |
| `-exit [status]` | ❌ | |
| `-since`/`-asince`/`-csince` | ❌ | |
| Whiteout type | ❌ | Platform-dependent |

## GNU extensions

| Feature | Status | Notes |
|---------|--------|-------|
| `-daystart` | ✅ | |
| `-warn`/`-nowarn` | ✅ | Accepted silently |
| `-noleaf` | ✅ | Accepted silently |
| `-ignore_readdir_race` | ✅ | |
| `-readable`/`-writable`/`-executable` | ✅ | access(2) |
| `-xtype` | ✅ | Opposite-deref type test |
| `-wholename`/`-iwholename` | ✅ | Path match aliases |
| `-ipath` | ✅ | |
| `-printf` | ✅ | Subset: %p %f %h %s %i %n %d %m %M %u %g %T |
| `-true`/`-false` | ✅ | |
| `-D` (debug) | ❌ | |
| `-O` (optimizer) | ❌ | |
| `-files0-from` | ❌ | |
| `-regextype` | ❌ | |
| `-newerXY` | ❌ | |
| `-ilname` | ❌ | |
| `-fls`/`-fprint`/`-fprint0`/`-fprintf` | ❌ | |

## Conflict resolution policy

| Conflict | Resolution |
|----------|-----------|
| Regex default flavor | BSD BRE (FreeBSD default); use `-E` for ERE |
| `-perm +mode` | Accepted as legacy alias for `/mode` (any-bit match) |
| `-perm -mode` | All-bits-set match (POSIX + BSD + GNU agree) |
| `-perm /mode` | Any-bit match (BSD/GNU standard form) |
| Implicit `-print` inhibitors | Any action in expression inhibits implicit `-print` |
| `-delete` + `-L` | `-delete` implies `-depth`, used without `-L` rejection (FreeBSD behavior) |
| `-follow` in expression | Sets global `-L` mode (BSD global-modifier semantics) |
| `-depth` in expression | Sets global post-order (BSD global-modifier semantics, not a predicate) |
