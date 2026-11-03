#!/bin/sh
#
# ar thin-archive path tests.  A thin archive stores the path of each
# member file relative to the directory holding the archive, as GNU ar
# stores it, and every reader (p, x, tv) opens a relative path against
# that directory, so the archive works from any current directory.
# GNU ar is the judge of the stored paths and of the layout: each path
# is in the "//" name table where GNU ar puts it, GNU ar reads the same
# member list from our archive as from its own, and each ar reads the
# other's thin archives.
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

# Print the path stored for the first member of thin archive $1, which
# has no symbol table: GNU ar keeps it in the "//" name table, the first
# member (header at offset 8, data at 68), as "path/\n".
stored_path() {
    [ "$(dd if="$1" bs=1 skip=8 count=16 2>/dev/null | tr -d ' ')" = "//" ] ||
        { printf '(no // table)'; return; }
    size=$(dd if="$1" bs=1 skip=56 count=10 2>/dev/null | tr -d ' ')
    dd if="$1" bs=1 skip=68 count="$size" 2>/dev/null | head -n 1 | sed 's,/$,,'
}

# Print the paths GNU ar reads from thin archive $1, as "ar t" prints
# them from the archive's own directory.
gnu_list() {
    (cd "$(dirname -- "$1")" && ar t "$(basename -- "$1")")
}

# Check that our archive $1 and GNU's archive $2, built the same way,
# store the same path in the same place, and that GNU ar reads the same
# member list from both.
same_path() {
    ours=$(stored_path "$1")
    gnus=$(stored_path "$2")
    [ -n "$gnus" ] || fail "$3: GNU ar stored no path"
    [ "$ours" = "$gnus" ] ||
        fail "$3: stored path '$ours', GNU ar stores '$gnus'"
    gnu_list "$2" > "$TMP/gnu.t"
    gnu_list "$1" > "$TMP/ours.t" 2>&1 ||
        fail "$3: GNU ar t cannot read our archive: $(cat "$TMP/ours.t")"
    cmp -s "$TMP/gnu.t" "$TMP/ours.t" ||
        fail "$3: GNU ar t lists '$(cat "$TMP/ours.t")', not '$(cat "$TMP/gnu.t")'"
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

    # 6. The layout is GNU's, so each ar reads the other's thin archive:
    #    a member is a header alone giving the file's size, with its
    #    path in the "//" table and no data after the header.
    printf 'odd' > obj/odd.o
    "$AR" rcT lib/two.a obj/foo.o obj/odd.o || fail "rcT lib/two.a failed"
    ar rcT lib/gtwo.a obj/foo.o obj/odd.o
    hdr=$((68 + $(dd if=lib/two.a bs=1 skip=56 count=10 2>/dev/null | tr -d ' ')))
    hdr=$((hdr + hdr % 2))
    want=$(wc -c < obj/foo.o | tr -d ' ')
    got=$(dd if=lib/two.a bs=1 skip=$((hdr + 48)) count=10 2>/dev/null | tr -d ' ')
    [ "$got" = "$want" ] || fail "thin member header gives size '$got', not $want"
    [ "$(wc -c < lib/two.a | tr -d ' ')" -eq $((hdr + 120)) ] ||
        fail "thin members are followed by data"
    ar p lib/two.a > gp.out 2>&1 || fail "GNU ar p cannot read our archive: $(cat gp.out)"
    cat obj/foo.o obj/odd.o | cmp -s - gp.out || fail "GNU ar p of our archive differs"
    "$AR" p lib/gtwo.a > op.out 2> op.err || fail "p of GNU's archive failed: $(cat op.err)"
    cat obj/foo.o obj/odd.o | cmp -s - op.out || fail "p of GNU's archive differs"
    [ "$("$AR" t lib/gtwo.a)" = "$(printf 'foo.o\nodd.o')" ] ||
        fail "t of GNU's archive lists '$("$AR" t lib/gtwo.a)'"
    cp obj/foo.o obj/bar.o
    "$AR" r lib/gtwo.a obj/bar.o || fail "r into GNU's archive failed"
    [ "$(gnu_list lib/gtwo.a)" = "$(printf '../obj/foo.o\n../obj/odd.o\n../obj/bar.o')" ] ||
        fail "GNU ar t after r lists '$(gnu_list lib/gtwo.a)'"
    pass "each ar reads the other's thin archive"
}

main "$@"
