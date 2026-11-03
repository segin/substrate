#!/bin/sh
#
# ar symbol-table tests: every offset in the GNU "/" member must name the
# file offset of the member header that defines the symbol, as GNU ar
# writes it.  GNU binutils (ar, nm, ld) is the judge.  The BSD
# "__.SYMDEF SORTED" table must be filled in the same way; BFD does not
# read BSD tables in ELF archives, so llvm-nm reads that one.  An
# archive that already has a table must get a new one when r, m or d
# rewrites it without s, as GNU ar does.  A thin archive gets a "/" table
# built from its external member files, as GNU ar builds one.
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

# Print the unsigned little-endian 32-bit word at byte offset $2 of file $1.
le32_at() {
    od -An -tu1 -j "$2" -N 4 "$1" |
        awk '{ print (($4 * 256 + $3) * 256 + $2) * 256 + $1 }'
}

# Check the BSD "__.SYMDEF SORTED" table of archive $1: it is the first
# member, named "#1/16" with the 16-byte name in front of its data, it
# holds at least one 8-byte (ran_strx, ran_off) entry, and every ran_off
# lands on a member header.
check_bsd_armap() {
    a=$1
    label=$2
    [ "$(od -An -tx1 -j 8 -N 16 "$a" | tr -s ' \n' ' ')" = \
        " 23 31 2f 31 36 20 20 20 20 20 20 20 20 20 20 20 " ] ||
        fail "$label: first ar_name is not '#1/16' padded with spaces"
    [ "$(name_at "$a" 8)" = "#1/16" ] ||
        fail "$label: first member is '$(name_at "$a" 8)', not #1/16"
    [ "$(dd if="$a" bs=1 skip=68 count=16 2>/dev/null)" = "__.SYMDEF SORTED" ] ||
        fail "$label: first member is not __.SYMDEF SORTED"
    ransize=$(le32_at "$a" 84)
    [ "$ransize" -gt 0 ] || fail "$label: empty symbol table"
    [ $((ransize % 8)) -eq 0 ] ||
        fail "$label: ranlib array is $ransize bytes, not a multiple of 8"
    count=$((ransize / 8))
    strsize=$(le32_at "$a" $((88 + ransize)))
    [ "$strsize" -gt 0 ] || fail "$label: empty string table"
    i=0
    while [ "$i" -lt "$count" ]; do
        strx=$(le32_at "$a" $((88 + 8 * i)))
        [ "$strx" -lt "$strsize" ] ||
            fail "$label: symbol $i string index $strx past $strsize"
        off=$(le32_at "$a" $((92 + 8 * i)))
        [ "$(fmag_at "$a" "$off")" = "600a" ] ||
            fail "$label: symbol $i offset $off is not a member header"
        n=$(name_at "$a" "$off")
        case "$n" in
        foo.o|baz.o|'#1/'[0-9]*) ;;
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

    # 5. BSD format: the "__.SYMDEF SORTED" table is filled in, and its
    #    offsets account for the "#1/16" name in front of the table.
    "$AR" --format=bsd rcs oursbsd.a foo.o averyveryverylongmembername.o baz.o
    check_bsd_armap oursbsd.a "BSD"
    pass "__.SYMDEF SORTED offsets name member headers"

    # 6. LLVM reads BSD symbol tables (GNU BFD does not, for ELF):
    #    llvm-nm must see the same symbol -> member map in our BSD
    #    archive as GNU nm sees in GNU ar's archive.
    if command -v llvm-nm >/dev/null 2>&1; then
        llvm-nm --print-armap oursbsd.a 2>&1 | grep ' in ' | sort > bsd.map
        cmp -s gnu.map bsd.map ||
            fail "BSD armap differs from GNU ar's: $(diff gnu.map bsd.map | head -5)"
        pass "llvm-nm --print-armap of the BSD archive agrees with GNU ar"
    else
        printf 'skip: llvm-nm not found, BSD armap not cross-checked\n'
    fi

    # 7. Writing an archive that has a symbol table rebuilds it without
    #    -s, as GNU ar does: after r, d and m the table must still name
    #    the right members at their new offsets.
    "$AR" rcs upd.a foo.o
    "$AR" r upd.a averyveryverylongmembername.o baz.o
    ar rcs gupd.a foo.o
    ar r gupd.a averyveryverylongmembername.o baz.o
    check_armap upd.a "r without s"
    nm --print-armap gupd.a 2>&1 | grep ' in ' | sort > gupd.map
    nm --print-armap upd.a 2>&1 | grep ' in ' | sort > upd.map
    cmp -s gupd.map upd.map ||
        fail "armap after r differs from GNU ar's: $(diff gupd.map upd.map | head -5)"
    cc -o prog2 main.o upd.a || fail "GNU ld could not link after r without s"
    pass "r without s rebuilds the symbol table"

    "$AR" m upd.a foo.o
    ar m gupd.a foo.o
    check_armap upd.a "m without s"
    nm --print-armap gupd.a 2>&1 | grep ' in ' | sort > gupd.map
    nm --print-armap upd.a 2>&1 | grep ' in ' | sort > upd.map
    cmp -s gupd.map upd.map ||
        fail "armap after m differs from GNU ar's: $(diff gupd.map upd.map | head -5)"
    pass "m without s rebuilds the symbol table"

    # Deleting every member that defines a symbol leaves no stale
    # entries behind.
    printf 'not an object\n' > note.txt
    "$AR" rcs del.a foo.o note.txt
    "$AR" d del.a foo.o
    ar rcs gdel.a foo.o note.txt
    ar d gdel.a foo.o
    nm --print-armap gdel.a 2>&1 | grep ' in ' | sort > gdel.map || :
    nm --print-armap del.a 2>&1 | grep ' in ' | sort > del.map || :
    cmp -s gdel.map del.map ||
        fail "armap after d differs from GNU ar's: $(diff gdel.map del.map | head -5)"
    pass "d without s rebuilds the symbol table"

    # An archive that holds an object file keeps a table, an empty one
    # when no object defines a symbol; with no object file it has none.
    # GNU ar writes "/" as a count of zero, four bytes.
    printf 'static int only(void) { return 0; }\n' > st.c
    cc -c -o st.o st.c
    "$AR" rcs empty.a foo.o st.o
    "$AR" d empty.a foo.o
    ar rcs gempty.a foo.o st.o
    ar d gempty.a foo.o
    "$AR" rcs empty2.a st.o
    ar rcs gempty2.a st.o
    for a in empty.a empty2.a; do
        for f in "$a" "g$a"; do
            [ "$(name_at "$f" 8)" = "/" ] || fail "$f: first member is '$(name_at "$f" 8)', not /"
            [ "$(dd if="$f" bs=1 skip=56 count=10 2>/dev/null | tr -d ' ')" = 4 ] ||
                fail "$f: / is not 4 bytes"
            [ "$(be32_at "$f" 68)" -eq 0 ] || fail "$f: / is not empty"
        done
    done
    [ "$(name_at gdel.a 8)" != "/" ] || fail "GNU ar indexes an archive with no object; the test is wrong"
    [ "$(name_at del.a 8)" != "/" ] || fail "an archive with no object file has a symbol table"
    pass "an archive of objects that define nothing has an empty table"

    # 8. BSD format: the table read from the archive is rebuilt at its
    #    new size, not written over its old buffer.
    "$AR" --format=bsd rcs bsdupd.a foo.o
    "$AR" --format=bsd r bsdupd.a averyveryverylongmembername.o baz.o ||
        fail "BSD r without s failed"
    check_bsd_armap bsdupd.a "BSD r without s"
    if command -v llvm-nm >/dev/null 2>&1; then
        llvm-nm --print-armap bsdupd.a 2>&1 | grep ' in ' | sort > bsdupd.map
        nm --print-armap gupd.a 2>&1 | grep ' in ' | sort > gupd.map
        cmp -s gupd.map bsdupd.map ||
            fail "BSD armap after r differs from GNU ar's: $(diff gupd.map bsdupd.map | head -5)"
    fi
    pass "BSD r without s rebuilds __.SYMDEF SORTED"

    # 9. Thin archives get a "/" table too, built from the external
    #    member files, as GNU ar builds one: it must map the same
    #    symbols to the same members as GNU ar's thin archive.
    "$AR" rcsT thin.a foo.o baz.o
    ar rcsT gthin.a foo.o baz.o
    [ "$(dd if=thin.a bs=1 count=8 2>/dev/null)" = '!<thin>' ] ||
        fail "rcsT did not write a thin archive"
    check_armap thin.a "thin"
    nm --print-armap gthin.a 2>&1 | grep ' in ' | sort > gthin.map
    nm --print-armap thin.a 2>&1 | grep ' in ' | sort > thin.map || :
    [ -s gthin.map ] || fail "GNU nm printed no armap for GNU's thin archive"
    cmp -s gthin.map thin.map ||
        fail "thin armap differs from GNU ar's: $(diff gthin.map thin.map | head -5)"
    pass "rcsT writes a / table for a thin archive"

    # 10. s on a thin archive in another directory reads each member
    #     relative to the archive's directory, not the current one.
    mkdir -p lib obj
    cp foo.o baz.o obj/
    (cd lib && "$AR" rcT thin.a ../obj/foo.o ../obj/baz.o) ||
        fail "rcT in a subdirectory failed"
    (cd lib && ar rcT gthin.a ../obj/foo.o ../obj/baz.o)
    "$AR" s lib/thin.a || fail "s on a thin archive in a subdirectory failed"
    ar s lib/gthin.a
    check_armap lib/thin.a "thin s from parent"
    nm --print-armap lib/gthin.a 2>&1 | grep ' in ' | sort > gthin2.map
    nm --print-armap lib/thin.a 2>&1 | grep ' in ' | sort > thin2.map || :
    [ -s gthin2.map ] || fail "GNU nm printed no armap for lib/gthin.a"
    cmp -s gthin2.map thin2.map ||
        fail "thin s armap differs from GNU ar's: $(diff gthin2.map thin2.map | head -5)"
    pass "s on a thin archive resolves members against the archive's directory"

    # 11. GNU ld links through our thin archive in another directory,
    #     whose stored paths differ from the members' names, and our s
    #     indexes GNU's thin archive as GNU ar does.
    cp averyveryverylongmembername.o obj/
    (cd lib && "$AR" rcsT all.a ../obj/foo.o \
        ../obj/averyveryverylongmembername.o ../obj/baz.o) ||
        fail "rcsT lib/all.a failed"
    (cd lib && ar rcsT gall.a ../obj/foo.o \
        ../obj/averyveryverylongmembername.o ../obj/baz.o)
    nm --print-armap lib/gall.a 2>&1 | grep ' in ' | sort > gall.map
    nm --print-armap lib/all.a 2>&1 | grep ' in ' | sort > all.map || :
    cmp -s gall.map all.map ||
        fail "thin armap with paths differs from GNU ar's: $(diff gall.map all.map | head -5)"
    cc -o prog3 main.o lib/all.a || fail "GNU ld could not link against our thin archive"
    ./prog3 || fail "program linked against our thin archive returned $?"
    (cd lib && ar rcT gidx.a ../obj/foo.o ../obj/averyveryverylongmembername.o ../obj/baz.o)
    "$AR" s lib/gidx.a || fail "s on GNU's thin archive failed"
    nm --print-armap lib/gidx.a 2>&1 | grep ' in ' | sort > gidx.map || :
    sed 's,gall\.a,gidx.a,' gall.map > gall2.map
    cmp -s gall2.map gidx.map ||
        fail "s on GNU's thin archive: $(diff gall2.map gidx.map | head -5)"
    pass "GNU nm and ld read our thin archive; our s indexes GNU's"

    # 12. p and x pass over the symbol and name tables, as GNU ar does:
    #     p prints only the members' bytes and x writes only the members.
    cat foo.o averyveryverylongmembername.o baz.o > members.cat
    ar p gnu.a > gnu.p
    cmp -s members.cat gnu.p || fail "GNU ar p gnu.a is not the members; the test is wrong"
    "$AR" p ourslong.a > ours.p || fail "p ourslong.a failed"
    cmp -s gnu.p ours.p || fail "p of an indexed archive prints more than the members"
    cat foo.o baz.o > thin.cat
    "$AR" p thin.a > thin.p || fail "p thin.a failed"
    cmp -s thin.cat thin.p || fail "p of an indexed thin archive prints more than the members"
    pass "p prints only the members"

    for a in ourslong.a thin.a oursbsd.a; do
        rm -rf xout
        mkdir xout
        (cd xout && "$AR" x "../$a") 2> x.err || fail "x $a failed"
        [ ! -s x.err ] || fail "x $a warned: $(cat x.err)"
        for f in xout/*; do
            case "$f" in
            xout/foo.o|xout/baz.o|xout/averyveryverylongmembername.o)
                cmp -s "$f" "${f#xout/}" || fail "x $a wrote the wrong bytes to $f" ;;
            *) fail "x $a wrote '$f', which is not a member" ;;
            esac
        done
    done
    pass "x writes only the members"
}

main "$@"
