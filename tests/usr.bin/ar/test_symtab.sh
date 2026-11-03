#!/bin/sh
#
# ar symbol-table tests: every offset in the GNU "/" member must name the
# file offset of the member header that defines the symbol, as GNU ar
# writes it.  GNU binutils (ar, nm, ld) is the judge.
#
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
AR="$ROOT/usr.bin/ar/ar"
TMP=$(mktemp -d "${TMPDIR:-/tmp}/ar-tests.XXXXXX")

cleanup() {
    rm -rf "$TMP"
}
trap cleanup EXIT INT TERM

pass() { printf 'ok: %s\n' "$1"; }
fail() { printf 'FAIL: %s\n' "$1" >&2; exit 1; }

require_cmd() {
    command -v "$1" >/dev/null 2>&1 || fail "missing command: $1"
}

# Print the unsigned big-endian 32-bit word at byte offset $2 of file $1.
be32_at() {
    od -An -tu1 -j "$2" -N 4 "$1" |
        awk '{ print (($1 * 256 + $2) * 256 + $3) * 256 + $4 }'
}

# Print the ar_name field of the member header at byte offset $2 of $1.
name_at() {
    dd if="$1" bs=1 skip="$2" count=16 2>/dev/null | tr -d ' '
}

# Print the two-byte ar_fmag field of the header at offset $2 of $1, as hex.
fmag_at() {
    od -An -tx1 -j $(($2 + 58)) -N 2 "$1" | tr -d ' '
}

# Check that every offset in the GNU "/" table of archive $1 lands on a
# member header ("`\n" trailer) whose name is one of the archive's members,
# and that the table member is exactly 4 + 4 * count + strings bytes.
check_armap() {
    a=$1
    label=$2
    [ "$(name_at "$a" 8)" = "/" ] || fail "$label: first member is not /"
    size=$(dd if="$a" bs=1 skip=56 count=10 2>/dev/null | tr -d ' ')
    count=$(be32_at "$a" 68)
    [ "$count" -gt 0 ] || fail "$label: empty symbol table"
    strbytes=$(dd if="$a" bs=1 skip=$((72 + 4 * count)) \
        count=$((size - 4 - 4 * count)) 2>/dev/null | tr -cd '\000' | wc -c)
    [ "$strbytes" -eq "$count" ] ||
        fail "$label: / is $size bytes for $count symbols (string table has $strbytes names)"
    i=0
    while [ "$i" -lt "$count" ]; do
        off=$(be32_at "$a" $((72 + 4 * i)))
        [ "$(fmag_at "$a" "$off")" = "600a" ] ||
            fail "$label: symbol $i offset $off is not a member header"
        n=$(name_at "$a" "$off")
        case "$n" in
        foo.o/|baz.o/|averyveryverylongmembername.o/|/[0-9]*) ;;
        *) fail "$label: symbol $i offset $off names member '$n'" ;;
        esac
        i=$((i + 1))
    done
}

main() {
    require_cmd cc
    require_cmd ar
    require_cmd nm
    require_cmd od

    [ -x "$AR" ] || fail "ar binary not found: $AR"

    cat > "$TMP/foo.c" <<'SRC'
int foo_data = 1;
int foo_bss;
int foo(void) { return foo_data + foo_bss; }
SRC
    cat > "$TMP/baz.c" <<'SRC'
int baz_data = 2;
int baz(void) { return baz_data; }
SRC
    cat > "$TMP/long.c" <<'SRC'
int quux_long(void) { return 3; }
SRC
    cat > "$TMP/main.c" <<'SRC'
extern int foo(void);
extern int baz(void);
extern int quux_long(void);
int main(void) { return foo() + baz() + quux_long() == 6 ? 0 : 1; }
SRC
    cc -c -o "$TMP/foo.o" "$TMP/foo.c"
    cc -c -o "$TMP/baz.o" "$TMP/baz.c"
    cc -c -o "$TMP/averyveryverylongmembername.o" "$TMP/long.c"
    cc -c -o "$TMP/main.o" "$TMP/main.c"

    cd "$TMP"

    # 1. Two short-named members.
    "$AR" rcs ours.a foo.o baz.o
    check_armap ours.a "short names"
    pass "/ offsets name member headers (short names)"

    # 2. A long name forces a "//" table between "/" and the members.
    "$AR" rcs ourslong.a foo.o averyveryverylongmembername.o baz.o
    check_armap ourslong.a "long names"
    pass "/ offsets name member headers (long names)"

    # 3. GNU nm reads the same symbol -> member map from our archive as
    #    from the one GNU ar builds from the same objects.
    ar rcs gnu.a foo.o averyveryverylongmembername.o baz.o
    nm --print-armap gnu.a 2>&1 | grep ' in ' | sort > gnu.map
    nm --print-armap ourslong.a 2>&1 | grep ' in ' | sort > ours.map
    [ -s gnu.map ] || fail "GNU nm printed no armap for the GNU archive"
    cmp -s gnu.map ours.map ||
        fail "armap differs from GNU ar's: $(diff gnu.map ours.map | head -5)"
    pass "GNU nm --print-armap agrees with GNU ar"

    # 4. GNU ld pulls members through the armap.
    cc -o prog main.o ourslong.a || fail "GNU ld could not link against our archive"
    ./prog || fail "program linked against our archive returned $?"
    pass "GNU ld links through the armap"
}

main "$@"
