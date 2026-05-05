#!/bin/sh
set -eu

CP_BIN=${1:-./cp_host}

fail() {
    echo "integration: FAIL: $*" >&2
    exit 1
}

assert_file_eq() {
    cmp -s "$1" "$2" || fail "files differ: $1 $2"
}

assert_exists() {
    [ -e "$1" ] || fail "missing path: $1"
}

TMPBASE=${TMPDIR:-/tmp}
WORK=$(mktemp -d "$TMPBASE/cp_int.XXXXXX")
trap 'rm -rf "$WORK"' EXIT INT TERM

# file -> file
printf 'hello world\n' > "$WORK/src1"
"$CP_BIN" "$WORK/src1" "$WORK/dst1"
assert_file_eq "$WORK/src1" "$WORK/dst1"

# files -> dir
mkdir "$WORK/dir"
printf 'A' > "$WORK/a"
printf 'B' > "$WORK/b"
"$CP_BIN" "$WORK/a" "$WORK/b" "$WORK/dir"
assert_file_eq "$WORK/a" "$WORK/dir/a"
assert_file_eq "$WORK/b" "$WORK/dir/b"

# recursive with -P and -L
mkdir -p "$WORK/tree/sub"
printf 'payload' > "$WORK/tree/sub/file"
ln -s sub/file "$WORK/tree/link_to_file"
"$CP_BIN" -R -P "$WORK/tree" "$WORK/tree_p"
[ -L "$WORK/tree_p/link_to_file" ] || fail "-P should preserve symlink"
"$CP_BIN" -R -L "$WORK/tree" "$WORK/tree_l"
[ -f "$WORK/tree_l/link_to_file" ] || fail "-L should follow symlink"

# -H follow command-line symlink only
ln -s "$WORK/tree" "$WORK/tree_cmd"
"$CP_BIN" -R -H "$WORK/tree_cmd" "$WORK/tree_h"
[ -d "$WORK/tree_h/sub" ] || fail "-H should follow command-line symlink"

# hardlink preservation under -a
mkdir "$WORK/hsrc"
printf 'hard' > "$WORK/hsrc/file1"
ln "$WORK/hsrc/file1" "$WORK/hsrc/file2"
"$CP_BIN" -a "$WORK/hsrc" "$WORK/hdst"
ino1=$(stat -c '%i' "$WORK/hdst/file1" 2>/dev/null || stat -f '%i' "$WORK/hdst/file1")
ino2=$(stat -c '%i' "$WORK/hdst/file2" 2>/dev/null || stat -f '%i' "$WORK/hdst/file2")
[ "$ino1" = "$ino2" ] || fail "hardlinks not preserved"

# -l hardlink mode
printf 'link me' > "$WORK/link_src"
"$CP_BIN" -l "$WORK/link_src" "$WORK/link_dst"
inoa=$(stat -c '%i' "$WORK/link_src" 2>/dev/null || stat -f '%i' "$WORK/link_src")
inob=$(stat -c '%i' "$WORK/link_dst" 2>/dev/null || stat -f '%i' "$WORK/link_dst")
[ "$inoa" = "$inob" ] || fail "-l should create hardlink"

# -s symlink mode
"$CP_BIN" -s "$WORK/link_src" "$WORK/sym_dst"
[ -L "$WORK/sym_dst" ] || fail "-s should create symlink"
[ "$(readlink "$WORK/sym_dst")" = "$WORK/link_src" ] || fail "-s target mismatch"

# sparse copy smoke
truncate -s 0 "$WORK/sparse_src"
dd if=/dev/zero of="$WORK/sparse_src" bs=1 count=1 seek=1048575 >/dev/null 2>&1
printf 'X' | dd of="$WORK/sparse_src" bs=1 count=1 seek=524288 conv=notrunc >/dev/null 2>&1
"$CP_BIN" "$WORK/sparse_src" "$WORK/sparse_dst"
ssz=$(stat -c '%s' "$WORK/sparse_src" 2>/dev/null || stat -f '%z' "$WORK/sparse_src")
dsz=$(stat -c '%s' "$WORK/sparse_dst" 2>/dev/null || stat -f '%z' "$WORK/sparse_dst")
[ "$ssz" = "$dsz" ] || fail "sparse size mismatch"

# preserve mode+timestamps
printf 'meta' > "$WORK/meta_src"
chmod 640 "$WORK/meta_src"
touch -t 202001010101 "$WORK/meta_src"
"$CP_BIN" -p "$WORK/meta_src" "$WORK/meta_dst"
ms=$(stat -c '%a' "$WORK/meta_src" 2>/dev/null || stat -f '%Mp%Lp' "$WORK/meta_src")
md=$(stat -c '%a' "$WORK/meta_dst" 2>/dev/null || stat -f '%Mp%Lp' "$WORK/meta_dst")
[ "$ms" = "$md" ] || fail "mode not preserved"

# xattr preservation best-effort
if python3 - <<'PY' >/dev/null 2>&1
import os, tempfile
fd, p = tempfile.mkstemp()
os.close(fd)
ok = hasattr(os, "setxattr")
if ok:
    os.setxattr(p, b"user.cp_test", b"value")
os.unlink(p)
PY
then
    printf 'xattr' > "$WORK/xattr_src"
    python3 - "$WORK/xattr_src" <<'PY'
import os, sys
os.setxattr(sys.argv[1], b"user.cp_test", b"value")
PY
    "$CP_BIN" -pa "$WORK/xattr_src" "$WORK/xattr_dst"
    python3 - "$WORK/xattr_dst" <<'PY'
import os, sys
assert os.getxattr(sys.argv[1], b"user.cp_test") == b"value"
PY
fi

# atomic replace: destination must end as complete file
printf 'old' > "$WORK/atomic_dst"
perl -e 'print "N" x 100000' > "$WORK/atomic_src"
"$CP_BIN" "$WORK/atomic_src" "$WORK/atomic_dst"
assert_file_eq "$WORK/atomic_src" "$WORK/atomic_dst"

# no-clobber
printf 'first' > "$WORK/nc_dst"
printf 'second' > "$WORK/nc_src"
"$CP_BIN" -n "$WORK/nc_src" "$WORK/nc_dst"
[ "$(cat "$WORK/nc_dst")" = "first" ] || fail "-n should not overwrite"

# --remove-destination should replace symlink itself, not its target
printf 'old-target' > "$WORK/rd_target"
printf 'new-data' > "$WORK/rd_src"
ln -s "$WORK/rd_target" "$WORK/rd_dst"
"$CP_BIN" --remove-destination "$WORK/rd_src" "$WORK/rd_dst"
[ ! -L "$WORK/rd_dst" ] || fail "--remove-destination should replace symlink path"
[ "$(cat "$WORK/rd_dst")" = "new-data" ] || fail "--remove-destination data mismatch"
[ "$(cat "$WORK/rd_target")" = "old-target" ] || fail "--remove-destination should not overwrite symlink target"

# interactive non-tty default no
printf 'orig' > "$WORK/i_dst"
printf 'new' > "$WORK/i_src"
"$CP_BIN" -i "$WORK/i_src" "$WORK/i_dst" < /dev/null
[ "$(cat "$WORK/i_dst")" = "orig" ] || fail "-i should default no on non-tty"

# recursive self-copy guard
mkdir -p "$WORK/self/src"
printf 'self' > "$WORK/self/src/file"
if "$CP_BIN" -R "$WORK/self/src" "$WORK/self/src/subdir" 2>/dev/null; then
    fail "recursive self-copy should fail"
fi

# --reflink semantics
printf 'reflink-data' > "$WORK/reflink_src"
"$CP_BIN" --reflink=auto "$WORK/reflink_src" "$WORK/reflink_auto"
assert_file_eq "$WORK/reflink_src" "$WORK/reflink_auto"
if "$CP_BIN" --reflink=always "$WORK/reflink_src" "$WORK/reflink_always" 2>/dev/null; then
    assert_file_eq "$WORK/reflink_src" "$WORK/reflink_always"
fi

# cross-filesystem hardlink failure (best effort)
if [ -d /dev/shm ]; then
    dev_a=$(stat -c '%d' "$WORK" 2>/dev/null || stat -f '%d' "$WORK")
    dev_b=$(stat -c '%d' /dev/shm 2>/dev/null || stat -f '%d' /dev/shm)
    if [ "$dev_a" != "$dev_b" ]; then
        printf 'x' > "$WORK/x"
        if "$CP_BIN" -l "$WORK/x" /dev/shm/cp_crossfs_test.$$ 2>/dev/null; then
            rm -f /dev/shm/cp_crossfs_test.$$ 
            fail "-l cross-filesystem should fail"
        fi
    fi
fi

echo "integration: PASS"
