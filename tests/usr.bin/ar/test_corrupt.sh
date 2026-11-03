#!/bin/sh
#
# ar corrupt-archive tests.  A member header with a bad ar_fmag
# trailer or an unparseable size, a GNU long-name reference outside the
# "//" name table, a member whose payload runs past the end of the
# file, and a header cut short by the end of the file all
# make the archive unreadable: ar must report it and exit non-zero,
# and an operation that rewrites the archive (r, d, s, ranlib) must
# leave the file exactly as it was instead of writing back whatever
# part of it was read.
#
# GNU ar is the judge where it reads the damage: it calls a bad
# trailer or size a "malformed archive" and fails, and it fails to
# rewrite an archive whose member data is truncated.  GNU only reads
# member data on demand, so its "t" lists a truncated archive and it
# treats a header cut short at the end as the end of the archive; ar
# reads every member up front, so it rejects both outright.  Non-numeric
# uid, gid and mode fields GNU ignores, and so does ar.
#
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
AR="$ROOT/usr.bin/ar/ar"
TMP=$(mktemp -d "${TMPDIR:-/tmp}/ar-corrupt.XXXXXX")

cleanup() {
    rm -rf "$TMP"
}
trap cleanup EXIT INT TERM

pass() { printf 'ok: %s\n' "$1"; }
fail() { printf 'FAIL: %s\n' "$1" >&2; exit 1; }

require_cmd() {
    command -v "$1" >/dev/null 2>&1 || fail "missing command: $1"
}

# Overwrite the bytes of file $1 at offset $2 with the string $3.
poke() {
    printf '%s' "$3" | dd of="$1" bs=1 seek="$2" conv=notrunc 2>/dev/null
}

# Copy the first $2 bytes of file $1 to file $3.
cut_file() {
    dd if="$1" of="$3" bs=1 count="$2" 2>/dev/null
}

# Run "$AR $2 <copy of $1> [args]" and require that it fails, prints a
# diagnostic and leaves the archive byte-for-byte unchanged.
must_reject() {
    src=$1
    op=$2
    shift 2
    cp "$src" "$TMP/work.a"
    if "$AR" "$op" "$TMP/work.a" "$@" > "$TMP/out" 2> "$TMP/err"; then
        fail "ar $op $(basename "$src") exited 0"
    fi
    [ -s "$TMP/err" ] || fail "ar $op $(basename "$src") printed no diagnostic"
    cmp -s "$src" "$TMP/work.a" ||
        fail "ar $op $(basename "$src") rewrote the archive"
    pass "ar $op rejects $(basename "$src")"
}

# Require that GNU ar also fails "ar $2" on a copy of archive $1.
gnu_rejects() {
    cp "$1" "$TMP/gnu.a"
    if ar "$2" "$TMP/gnu.a" c.txt > /dev/null 2>&1; then
        fail "GNU ar $2 accepts $(basename "$1"); the test is wrong"
    fi
}

main() {
    require_cmd ar

    cd "$TMP"
    printf 'hello' > a.txt                 # odd size: padded
    printf 'second member\n' > b.txt
    printf 'c' > c.txt
    ar rcU good.a a.txt b.txt

    # The first member header starts at offset 8: ar_uid at +28,
    # ar_mode at +40, ar_size at +48, ar_fmag at +58; its data at 68.
    size=$(wc -c < good.a | tr -d ' ')

    cp good.a fmag.a;  poke fmag.a 66 'xx'
    cp good.a size.a;  poke size.a 56 'abc       '
    cp good.a big.a;   poke big.a 56 '999999    '
    cut_file good.a $((size - 3)) tpay.a
    cut_file good.a $((68 + 6 + 30)) thdr.a
    cp good.a uid.a;   poke uid.a 36 'zz    '
    cp good.a mode.a;  poke mode.a 48 'zz      '

    # The sound archive lists cleanly.
    out=$("$AR" t good.a) || fail "ar t good.a failed"
    [ "$out" = "$(printf 'a.txt\nb.txt')" ] || fail "ar t good.a: '$out'"
    pass "ar t good.a"

    # GNU agrees that these are broken.
    gnu_rejects fmag.a t
    gnu_rejects size.a t
    gnu_rejects tpay.a r
    gnu_rejects big.a r

    for a in fmag.a size.a big.a tpay.a thdr.a; do
        must_reject "$a" t
        must_reject "$a" tv
        must_reject "$a" p
        must_reject "$a" r c.txt
        must_reject "$a" d a.txt
        must_reject "$a" s
    done

    # x extracts nothing from a broken archive.
    mkdir xdir
    if (cd xdir && "$AR" x ../tpay.a) 2> /dev/null; then
        fail "ar x tpay.a exited 0"
    fi
    pass "ar x rejects tpay.a"

    # A GNU "/<offset>" long-name reference that points at or past the
    # end of the "//" name table, or that comes with no name table at
    # all, names nothing: it must not become a member with an empty
    # name.  GNU calls the first a malformed archive; with no table it
    # lists an empty name, which ar rejects as well.
    long=a_member_with_a_rather_long_name.txt
    printf 'long' > "$long"
    ar rcU lname.a "$long" b.txt
    out=$("$AR" t lname.a) || fail "ar t lname.a failed"
    [ "$out" = "$(printf '%s\nb.txt' "$long")" ] || fail "ar t lname.a: '$out'"
    pass "ar t lname.a"
    # "//" is the first member: its size at 56, its data at 68.
    tsize=$(dd if=lname.a bs=1 skip=56 count=10 2>/dev/null | tr -d ' ')
    hdr2=$((68 + tsize + tsize % 2))        # the long-named member's
    cp lname.a lfar.a;  poke lfar.a "$hdr2" '/999999'
    cp lname.a lend.a;  poke lend.a "$hdr2" "/$tsize"
    cp good.a lnotab.a; poke lnotab.a 8 '/0    '
    # A reference is the digits after the '/': GNU reads them and
    # ignores what follows, so "/9999x" is past the table and "/0x"
    # names the first entry.  Neither may fall back to the name before
    # the '/', which is empty.
    cp lname.a ljunk.a; poke ljunk.a "$hdr2" '/9999x'
    cp lname.a lzero.a; poke lzero.a "$hdr2" '/0x'
    gnu_rejects lfar.a t
    gnu_rejects lend.a t
    gnu_rejects ljunk.a t
    for a in lfar.a lend.a lnotab.a ljunk.a; do
        must_reject "$a" t
        must_reject "$a" p
        must_reject "$a" r c.txt
        must_reject "$a" d a.txt
        grep -q 'malformed archive' "$TMP/err" ||
            fail "ar d $a: $(cat "$TMP/err")"
    done
    want=$(ar t lzero.a) || fail "GNU ar t lzero.a failed; the test is wrong"
    out=$("$AR" t lzero.a) || fail "ar t lzero.a failed"
    [ "$out" = "$want" ] || fail "ar t lzero.a: '$out', GNU ar lists '$want'"
    ar p lzero.a "$long" > lzero.gnu
    "$AR" p lzero.a "$long" > lzero.out || fail "ar p lzero.a failed"
    [ -s lzero.gnu ] || fail "GNU ar p lzero.a printed nothing; the test is wrong"
    cmp -s lzero.gnu lzero.out || fail "ar p lzero.a differs from GNU ar p"
    pass "ar resolves a reference with trailing junk as GNU ar does"

    # ranlib rewrites the archive too.
    ln -s "$AR" "$TMP/ranlib"
    for a in fmag.a tpay.a; do
        cp "$a" work.a
        if "$TMP/ranlib" work.a 2> /dev/null; then
            fail "ranlib $a exited 0"
        fi
        cmp -s "$a" work.a || fail "ranlib $a rewrote the archive"
        pass "ranlib rejects $a"
    done

    # Fields GNU does not validate stay accepted.
    for a in uid.a mode.a; do
        ar t "$a" > /dev/null 2>&1 || fail "GNU ar t $a failed"
        out=$("$AR" t "$a" 2> /dev/null) || fail "ar t $a failed"
        [ "$out" = "$(printf 'a.txt\nb.txt')" ] || fail "ar t $a: '$out'"
        pass "ar t accepts $a"
    done
}

main "$@"
