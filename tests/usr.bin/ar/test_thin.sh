#!/bin/sh
#
# ar thin-archive path tests.  A thin archive stores the path of each
# member file relative to the directory holding the archive, as GNU ar
# stores it, and every reader (p, x, tv) opens a relative path against
# that directory, so the archive works from any current directory.
# GNU ar is the judge of the stored paths: run in the archive's own
# directory, "ar t" on GNU's thin archive prints the path it stored.
#
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
AR="$ROOT/usr.bin/ar/ar"
TMP=$(mktemp -d "${TMPDIR:-/tmp}/ar-thin.XXXXXX")

cleanup() {
    rm -rf "$TMP"
}
trap cleanup EXIT INT TERM

pass() { printf 'ok: %s\n' "$1"; }
fail() { printf 'FAIL: %s\n' "$1" >&2; exit 1; }

require_cmd() {
    command -v "$1" >/dev/null 2>&1 || fail "missing command: $1"
}

# Print the path stored as the payload of the first member of our thin
# archive $1 (no symbol table: the member header is at offset 8).
our_path() {
    size=$(dd if="$1" bs=1 skip=56 count=10 2>/dev/null | tr -d ' ')
    dd if="$1" bs=1 skip=68 count="$size" 2>/dev/null
}

# Print the path GNU ar stored for the first member of its thin archive
# $1, as "ar t" prints it from the archive's own directory.
gnu_path() {
    (cd "$(dirname -- "$1")" && ar t "$(basename -- "$1")" | head -n 1)
}

# Check that our archive $1 and GNU's archive $2, built the same way,
# store the same path.
same_path() {
    ours=$(our_path "$1")
    gnus=$(gnu_path "$2")
    [ -n "$gnus" ] || fail "$3: GNU ar stored no path"
    [ "$ours" = "$gnus" ] ||
        fail "$3: stored path '$ours', GNU ar stores '$gnus'"
}

main() {
    require_cmd ar

    mkdir "$TMP/subdir" "$TMP/obj" "$TMP/lib" "$TMP/elsewhere"
    printf 'subdir member\n' > "$TMP/subdir/sub.o"
    printf 'obj member contents\n' > "$TMP/obj/foo.o"
    cd "$TMP"

    # 1. Archive made inside its directory, read from the parent.
    (cd subdir && "$AR" rcT lib.a sub.o) || fail "rcT in subdir failed"
    (cd subdir && ar rcT glib.a sub.o)
    same_path subdir/lib.a subdir/glib.a "made in subdir"
    "$AR" p subdir/lib.a > p.out 2> p.err ||
        fail "p from the parent failed"
    [ ! -s p.err ] || fail "p from the parent warned: $(cat p.err)"
    cmp -s p.out subdir/sub.o || fail "p from the parent printed the wrong bytes"
    pass "p resolves a thin member against the archive's directory"

    want=$(wc -c < subdir/sub.o | tr -d ' ')
    got=$("$AR" tv subdir/lib.a | awk '{ print $3 }')
    [ "$got" = "$want" ] || fail "tv from the parent shows size $got, not $want"
    pass "tv sizes a thin member from the archive's directory"

    (cd elsewhere && "$AR" x ../subdir/lib.a) 2> x.err ||
        fail "x from another directory failed"
    [ ! -s x.err ] || fail "x from another directory warned: $(cat x.err)"
    cmp -s elsewhere/sub.o subdir/sub.o ||
        fail "x from another directory wrote the wrong bytes"
    pass "x resolves a thin member against the archive's directory"

    # 2. Archive made from the parent, naming the file by its path from
    #    there: it stores the path from the archive's directory, so it
    #    reads from inside that directory too.
    "$AR" rcT subdir/lib2.a subdir/sub.o || fail "rcT from the parent failed"
    ar rcT subdir/glib2.a subdir/sub.o
    same_path subdir/lib2.a subdir/glib2.a "made from the parent"
    "$AR" p subdir/lib2.a | cmp -s - subdir/sub.o ||
        fail "p of lib2.a from the parent printed the wrong bytes"
    (cd subdir && "$AR" p lib2.a) | cmp -s - subdir/sub.o ||
        fail "p of lib2.a from its own directory printed the wrong bytes"
    pass "a member named from the parent is stored relative to the archive"

    # 3. A member outside the archive's directory gets a ../ path.
    "$AR" rcT lib/up.a obj/foo.o || fail "rcT lib/up.a failed"
    ar rcT lib/gup.a obj/foo.o
    same_path lib/up.a lib/gup.a "member in a sibling directory"
    (cd elsewhere && "$AR" p ../lib/up.a) | cmp -s - obj/foo.o ||
        fail "p of lib/up.a from elsewhere printed the wrong bytes"
    (cd lib && "$AR" rcT up2.a ../obj/foo.o) || fail "rcT ../obj/foo.o failed"
    (cd lib && ar rcT gup2.a ../obj/foo.o)
    same_path lib/up2.a lib/gup2.a "../ member named inside the directory"
    "$AR" p lib/up2.a | cmp -s - obj/foo.o ||
        fail "p of lib/up2.a from the parent printed the wrong bytes"
    pass "a member in a sibling directory is stored with ../"

    # 4. An absolute path is stored as given and read from anywhere.
    "$AR" rcT subdir/abs.a "$TMP/obj/foo.o" || fail "rcT absolute failed"
    ar rcT subdir/gabs.a "$TMP/obj/foo.o"
    same_path subdir/abs.a subdir/gabs.a "absolute member"
    (cd / && "$AR" p "$TMP/subdir/abs.a") | cmp -s - obj/foo.o ||
        fail "p of an absolute member printed the wrong bytes"
    pass "an absolute member path is stored as given"

    # 5. A member whose file is gone is reported, and x does not leave
    #    an empty file in its place.
    cp subdir/sub.o subdir/gone.o
    (cd subdir && "$AR" rcT gone.a gone.o) || fail "rcT gone.a failed"
    rm subdir/gone.o
    (cd elsewhere && "$AR" x ../subdir/gone.a) 2> gone.err || :
    [ -s gone.err ] || fail "x of a missing thin member did not warn"
    [ ! -e elsewhere/gone.o ] ||
        fail "x of a missing thin member created an empty file"
    pass "x of a missing thin member creates nothing"
}

main "$@"
