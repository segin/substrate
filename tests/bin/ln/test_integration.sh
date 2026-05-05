#!/bin/sh
set -eu

LN_BIN=${1:-./ln_host}
WORK=$(mktemp -d "${TMPDIR:-/tmp}/ln_test.XXXXXX")
trap 'rm -rf "$WORK"' EXIT INT TERM

fail() {
    echo "FAIL: $1" >&2
    exit 1
}

inode_id() {
    stat -c '%d:%i' "$1" 2>/dev/null || stat -f '%d:%i' "$1"
}

assert_symlink_target() {
    got=$(readlink "$1")
    [ "$got" = "$2" ] || fail "expected symlink $1 -> $2 (got $got)"
}

run_ok() {
    if ! "$LN_BIN" "$@" >"$WORK/out" 2>"$WORK/err"; then
        cat "$WORK/err" >&2
        fail "expected success: $*"
    fi
}

run_fail() {
    if "$LN_BIN" "$@" >"$WORK/out" 2>"$WORK/err"; then
        fail "expected failure: $*"
    fi
}

# POSIX hard-link baseline + no stdout baseline.
printf 'a' > "$WORK/a"
run_ok "$WORK/a" "$WORK/b"
[ "$(inode_id "$WORK/a")" = "$(inode_id "$WORK/b")" ] || fail "hard link inode mismatch"
[ ! -s "$WORK/out" ] || fail "stdout must be empty in non-verbose mode"

# Existing destination without replacement options must fail.
printf 'src' > "$WORK/src"
printf 'dst' > "$WORK/dst"
run_fail "$WORK/src" "$WORK/dst"

# Force replacement.
run_ok -f "$WORK/src" "$WORK/dst"
[ "$(inode_id "$WORK/src")" = "$(inode_id "$WORK/dst")" ] || fail "-f did not replace destination"

# Self-link safety.
run_fail -f "$WORK/src" "$WORK/src"
[ -f "$WORK/src" ] || fail "self-link safety corrupted source"

# Symbolic links allow dangling targets.
run_ok -s "$WORK/missing-target" "$WORK/sym"
[ -L "$WORK/sym" ] || fail "-s did not create symlink"
assert_symlink_target "$WORK/sym" "$WORK/missing-target"

# Default dereference mode is BSD-preferred -L.
printf 't' > "$WORK/target"
ln -s "$WORK/target" "$WORK/src_link"
run_ok "$WORK/src_link" "$WORK/default_logical"
[ "$(inode_id "$WORK/target")" = "$(inode_id "$WORK/default_logical")" ] || fail "default mode should behave as -L"

# -P must hard-link the symlink itself.
run_ok -P "$WORK/src_link" "$WORK/physical"
[ "$(inode_id "$WORK/src_link")" = "$(inode_id "$WORK/physical")" ] || fail "-P should link symlink inode"
[ -L "$WORK/physical" ] || fail "-P result should be symlink"

# Multi-source requires destination directory.
printf 'x' > "$WORK/x"
printf 'y' > "$WORK/y"
run_fail "$WORK/x" "$WORK/y" "$WORK/notadir"

mkdir "$WORK/dir"
run_ok "$WORK/x" "$WORK/y" "$WORK/dir"
[ -e "$WORK/dir/$(basename "$WORK/x")" ] || fail "multi-source missing first link"
[ -e "$WORK/dir/$(basename "$WORK/y")" ] || fail "multi-source missing second link"

# -t/-T mutual exclusion.
run_fail -t "$WORK/dir" -T "$WORK/x"

# BSD -n/-h behavior with symlinked destination directory.
mkdir "$WORK/realdir"
ln -s "$WORK/realdir" "$WORK/dstlink"
run_ok -s -n -f "$WORK/new-target" "$WORK/dstlink"
[ -L "$WORK/dstlink" ] || fail "-n should preserve destination symlink path"
assert_symlink_target "$WORK/dstlink" "$WORK/new-target"

# BSD -F with -s replaces existing directory target.
mkdir "$WORK/replace_dir"
run_ok -s -F "$WORK/f-target" "$WORK/replace_dir"
[ -L "$WORK/replace_dir" ] || fail "-F with -s should replace directory"
assert_symlink_target "$WORK/replace_dir" "$WORK/f-target"

# Option order precedence: -f overrides prior -i, -i overrides prior -f.
printf 'z' > "$WORK/zsrc"
printf 'old' > "$WORK/zdst"
printf 'n\n' | "$LN_BIN" -f -i "$WORK/zsrc" "$WORK/zdst" >"$WORK/out" 2>"$WORK/err" && fail "-f -i should have required confirmation and failed on no"

printf 'n\n' | "$LN_BIN" -i -f "$WORK/zsrc" "$WORK/zdst" >"$WORK/out" 2>"$WORK/err" || fail "-i -f should force replace"
[ "$(inode_id "$WORK/zsrc")" = "$(inode_id "$WORK/zdst")" ] || fail "-i -f did not force replace"

# -w warns for missing symlink source but still creates.
if ! "$LN_BIN" -s -w "$WORK/never-there" "$WORK/wsym" >"$WORK/out" 2>"$WORK/err"; then
    fail "-w with dangling source should still succeed"
fi
[ -L "$WORK/wsym" ] || fail "-w case did not create symlink"
grep -qi "warning" "$WORK/err" || fail "-w should emit warning"

# Backups: simple, suffix override, numbered.
printf 'b' > "$WORK/bsrc"
printf 'old' > "$WORK/bdst"
run_ok -f -b "$WORK/bsrc" "$WORK/bdst"
[ -e "$WORK/bdst~" ] || fail "simple backup missing"

printf 'old2' > "$WORK/bsfx"
run_ok -f -b -S .bak "$WORK/bsrc" "$WORK/bsfx"
[ -e "$WORK/bsfx.bak" ] || fail "suffix backup missing"

printf 'ns1' > "$WORK/bnum_src1"
printf 'n1' > "$WORK/bnum"
run_ok -f --backup=numbered "$WORK/bnum_src1" "$WORK/bnum"
[ -e "$WORK/bnum.~1~" ] || fail "numbered backup .~1~ missing"
printf 'ns2' > "$WORK/bnum_src2"
printf 'n2' > "$WORK/bnum"
run_ok -f --backup=numbered "$WORK/bnum_src2" "$WORK/bnum"
[ -e "$WORK/bnum.~2~" ] || fail "numbered backup .~2~ missing"

# GNU -r relative symlink.
mkdir -p "$WORK/rel/a" "$WORK/rel/b"
printf 'rel' > "$WORK/rel/a/t"
run_ok -s -r "$WORK/rel/a/t" "$WORK/rel/b/l"
assert_symlink_target "$WORK/rel/b/l" "../a/t"

# Verbose output must include destination and source/link-target.
run_ok -v "$WORK/src" "$WORK/vdst"
grep -q "$WORK/vdst" "$WORK/out" || fail "-v output missing destination"
grep -q "$WORK/src" "$WORK/out" || fail "-v output missing source"

echo "ln integration tests: PASS"
