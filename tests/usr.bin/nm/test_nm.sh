#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
NM="$ROOT/usr.bin/nm/nm"
TMP=$(mktemp -d "${TMPDIR:-/tmp}/nm-tests.XXXXXX")

cleanup() {
    rm -rf "$TMP"
}
trap cleanup EXIT INT TERM

pass() { printf 'ok: %s\n' "$1"; }
fail() { printf 'FAIL: %s\n' "$1" >&2; exit 1; }

require_cmd() {
    command -v "$1" >/dev/null 2>&1 || fail "missing command: $1"
}

expect_match() {
    label=$1
    text=$2
    pattern=$3
    printf '%s\n' "$text" | grep -E "$pattern" >/dev/null 2>&1 || {
        printf 'FAIL: %s\npattern: %s\ntext:\n%s\n' "$label" "$pattern" "$text" >&2
        exit 1
    }
    pass "$label"
}

expect_nomatch() {
    label=$1
    text=$2
    pattern=$3
    printf '%s\n' "$text" | grep -E "$pattern" >/dev/null 2>&1 && {
        printf 'FAIL: %s\nunexpected pattern: %s\ntext:\n%s\n' "$label" "$pattern" "$text" >&2
        exit 1
    }
    pass "$label"
}

main() {
    require_cmd cc
    require_cmd ar
    require_cmd objcopy

    [ -x "$NM" ] || fail "nm binary not found: $NM"

    cat > "$TMP/sample.c" <<'SRC'
int g_data = 7;
int g_bss;
static int s_data = 3;
static int s_bss;
extern int ext_ref;

int use_ext(void) {
    return ext_ref + g_data + g_bss + s_data + s_bss;
}
SRC

    cc -c -o "$TMP/sample.o" "$TMP/sample.c"

    OUT=$($NM "$TMP/sample.o")
    expect_match "object has global text" "$OUT" '[[:space:]]T[[:space:]]+use_ext$'
    expect_match "object has global data" "$OUT" '[[:space:]]D[[:space:]]+g_data$'
    expect_match "object has global bss" "$OUT" '[[:space:]]B[[:space:]]+g_bss$'
    expect_match "object has local data" "$OUT" '[[:space:]]d[[:space:]]+s_data$'
    expect_match "object has local bss" "$OUT" '[[:space:]]b[[:space:]]+s_bss$'
    expect_match "object has undefined" "$OUT" '[[:space:]]U[[:space:]]+ext_ref$'

    OUT_U=$($NM -u "$TMP/sample.o")
    expect_match "-u keeps undefined" "$OUT_U" '[[:space:]]U[[:space:]]+ext_ref$'
    expect_nomatch "-u drops defined text" "$OUT_U" '[[:space:]]T[[:space:]]+use_ext$'

    OUT_G=$($NM -g "$TMP/sample.o")
    expect_match "-g keeps external" "$OUT_G" '[[:space:]]D[[:space:]]+g_data$'
    expect_nomatch "-g drops local" "$OUT_G" '[[:space:]]d[[:space:]]+s_data$'

    OUT_P=$($NM -P "$TMP/sample.o")
    expect_match "-P format includes name/type/value/size" "$OUT_P" '^use_ext[[:space:]]+T[[:space:]]+[0-9a-fA-F]+[[:space:]]+[0-9a-fA-F]+$'

    cat > "$TMP/sample.cpp" <<'SRC'
namespace Ns {
int FooBar(int x) { return x + 1; }
}
SRC
    c++ -c -o "$TMP/sample_cpp.o" "$TMP/sample.cpp"

    OUT_RAW=$($NM "$TMP/sample_cpp.o")
    expect_match "mangled C++ visible without -C" "$OUT_RAW" '_ZN2Ns6FooBarEi'

    OUT_DEM=$($NM -C "$TMP/sample_cpp.o")
    expect_match "demangled with -C" "$OUT_DEM" 'Ns::FooBar\(int\)'

    cat > "$TMP/a.c" <<'SRC'
int alpha(void) { return 1; }
SRC
    cat > "$TMP/b.c" <<'SRC'
int beta(void) { return 2; }
SRC
    cc -c -o "$TMP/a.o" "$TMP/a.c"
    cc -c -o "$TMP/b.o" "$TMP/b.c"
    ar rcs "$TMP/libab.a" "$TMP/a.o" "$TMP/b.o"

    OUT_AR=$($NM "$TMP/libab.a")
    expect_match "archive shows first member" "$OUT_AR" 'libab\.a\[a\.o\]:'
    expect_match "archive shows second member" "$OUT_AR" 'libab\.a\[b\.o\]:'
    expect_match "archive member symbol alpha" "$OUT_AR" '[[:space:]]T[[:space:]]+alpha$'
    expect_match "archive member symbol beta" "$OUT_AR" '[[:space:]]T[[:space:]]+beta$'
    expect_nomatch "no archive index without -s" "$OUT_AR" '^Archive index:$'

    # -s / --print-armap: the archive's symbol index comes first, one
    # "<symbol> in <member>" line per entry, members named through the
    # long-name table too, symbols demangled under -C.
    cat > "$TMP/a_rather_long_member_name.cpp" <<'SRC'
namespace Ns { int Indexed(int x) { return x * 2; } }
int gamma_var = 4;
SRC
    c++ -c -o "$TMP/a_rather_long_member_name.o" "$TMP/a_rather_long_member_name.cpp"
    ar rcs "$TMP/libidx.a" "$TMP/a.o" "$TMP/b.o" "$TMP/a_rather_long_member_name.o"

    OUT_S=$($NM -s "$TMP/libidx.a")
    expect_match "-s prints archive index header" "$OUT_S" '^Archive index:$'
    expect_match "-s indexes alpha in a.o" "$OUT_S" '^alpha in a\.o$'
    expect_match "-s indexes beta in b.o" "$OUT_S" '^beta in b\.o$'
    expect_match "-s names long members" "$OUT_S" '^gamma_var in a_rather_long_member_name\.o$'
    expect_match "-s keeps member listing" "$OUT_S" '[[:space:]]T[[:space:]]+beta$'
    OUT_S2=$($NM --print-armap "$TMP/libidx.a")
    [ "$OUT_S" = "$OUT_S2" ] || fail "--print-armap matches -s"
    pass "--print-armap matches -s"
    OUT_SC=$($NM -s -C "$TMP/libidx.a")
    expect_match "-s -C demangles index" "$OUT_SC" '^Ns::Indexed\(int\) in a_rather_long_member_name\.o$'

    ar rcsT "$TMP/libthin.a" "$TMP/a.o" "$TMP/a_rather_long_member_name.o"
    OUT_ST=$($NM -s "$TMP/libthin.a")
    expect_match "-s on thin archive indexes alpha" "$OUT_ST" '^alpha in .*a\.o$'

    # A thin archive stores no member data: each header's size field is
    # the external file's size, and the next header follows directly.
    # Every member must be listed, not just the first.
    cat > "$TMP/c.c" <<'SRC'
int gamma_fn(void) { return 3; }
SRC
    cc -c -o "$TMP/c.o" "$TMP/c.c"
    ar rcT "$TMP/libthin3.a" "$TMP/a.o" "$TMP/b.o" "$TMP/c.o"
    OUT_T3=$($NM "$TMP/libthin3.a") || fail "nm on thin archive exits 0"
    expect_match "thin archive lists first member" "$OUT_T3" '[[:space:]]T[[:space:]]+alpha$'
    expect_match "thin archive lists second member" "$OUT_T3" '[[:space:]]T[[:space:]]+beta$'
    expect_match "thin archive lists third member" "$OUT_T3" '[[:space:]]T[[:space:]]+gamma_fn$'

    if nm --version 2>/dev/null | grep -q 'GNU nm'; then
        armap_index() {
            awk '/^Archive index:$/ { on = 1 } on && /^$/ { exit } on { print }'
        }
        for flags in "-s" "-s -C"; do
            # shellcheck disable=SC2086
            GOT=$($NM $flags "$TMP/libidx.a" | armap_index)
            # shellcheck disable=SC2086
            WANT=$(nm $flags "$TMP/libidx.a" | armap_index)
            [ -n "$WANT" ] && [ "$GOT" = "$WANT" ] || {
                printf 'FAIL: nm %s archive index differs from GNU nm\n--- got:\n%s\n--- want:\n%s\n' \
                    "$flags" "$GOT" "$WANT" >&2
                exit 1
            }
            pass "nm $flags archive index matches GNU nm"
        done
        GOT=$($NM -s "$TMP/libthin.a" | armap_index)
        WANT=$(nm -s "$TMP/libthin.a" | armap_index)
        [ "$GOT" = "$WANT" ] || {
            printf 'FAIL: nm -s thin archive index differs from GNU nm\n--- got:\n%s\n--- want:\n%s\n' \
                "$GOT" "$WANT" >&2
            exit 1
        }
        pass "nm -s thin archive index matches GNU nm"
        # Symbol lines only: the member headings name thin members
        # differently and are not what this checks.
        GOT=$($NM "$TMP/libthin3.a" | grep -v ':$')
        WANT=$(nm "$TMP/libthin3.a" | grep -v ':$')
        [ "$GOT" = "$WANT" ] || {
            printf 'FAIL: nm thin archive listing differs from GNU nm\n--- got:\n%s\n--- want:\n%s\n' \
                "$GOT" "$WANT" >&2
            exit 1
        }
        pass "nm thin archive listing matches GNU nm"
    fi

    # --size-sort: ascending by size, undefined and size-0 symbols are
    # dropped, and without -S the BSD value column carries the size.
    cat > "$TMP/sizes.c" <<'SRC'
extern int ext_sz;
char big_arr[300] = { 1 };
short mid_arr[40] = { 2 };
int small_var = 3;
__asm__(".globl zero_sym\n.data\nzero_sym:\n.text\n");
int sz_fn(void) { return ext_sz + big_arr[0] + mid_arr[0] + small_var; }
SRC
    cc -c -o "$TMP/sizes.o" "$TMP/sizes.c"

    OUT_SS=$($NM --size-sort "$TMP/sizes.o")
    expect_nomatch "--size-sort drops undefined" "$OUT_SS" 'ext_sz'
    expect_nomatch "--size-sort drops size-0 symbols" "$OUT_SS" 'zero_sym'
    expect_match "--size-sort prints size of small_var" "$OUT_SS" '^0+4 D small_var$'
    expect_match "--size-sort prints size of mid_arr" "$OUT_SS" '^0+50 D mid_arr$'
    expect_match "--size-sort prints size of big_arr" "$OUT_SS" '^0+12c D big_arr$'
    ORDER=$(printf '%s\n' "$OUT_SS" | awk '{print $3}' | grep -E '^(small_var|mid_arr|big_arr)$' | tr '\n' ' ')
    [ "$ORDER" = "small_var mid_arr big_arr " ] ||
        fail "--size-sort order is ascending (got: $ORDER)"
    pass "--size-sort order is ascending"

    OUT_SSS=$($NM --size-sort -S "$TMP/sizes.o")
    # Both the value and the size (4) are shown; the -S column order
    # itself is not what this checks.
    NF_SV=$(printf '%s\n' "$OUT_SSS" | awk '$NF == "small_var" { n = 0; for (i = 1; i < NF; i++) if ($i ~ /^0+4$/) n++; print NF ":" n }')
    [ "$NF_SV" = "4:1" ] ||
        fail "--size-sort -S prints value and size (got: $OUT_SSS)"
    pass "--size-sort -S prints value and size"
    expect_nomatch "--size-sort -S drops undefined" "$OUT_SSS" 'ext_sz'

    if nm --version 2>/dev/null | grep -q 'GNU nm'; then
        for flags in "--size-sort" "--size-sort -r" "--size-sort -g" "--size-sort -u"; do
            # shellcheck disable=SC2086
            GOT=$($NM $flags "$TMP/sizes.o")
            # shellcheck disable=SC2086
            WANT=$(nm $flags "$TMP/sizes.o")
            [ "$GOT" = "$WANT" ] || {
                printf 'FAIL: nm %s differs from GNU nm\n--- got:\n%s\n--- want:\n%s\n' \
                    "$flags" "$GOT" "$WANT" >&2
                exit 1
            }
            pass "nm $flags matches GNU nm"
        done
    fi

    # GNU symbol classes: an IFUNC is 'i' whatever its binding (even
    # weak or local), a unique global is 'u', and an STT_COMMON symbol
    # is typed "COMMON" in the System V format.
    cat > "$TMP/gnuext.c" <<'SRC'
static int impl(void) { return 1; }
static int (*resolve(void))(void) { return impl; }
int g_ifunc(void) __attribute__((ifunc("resolve")));
static int l_ifunc(void) __attribute__((ifunc("resolve")));
int com_var;
int use_l(void) { return l_ifunc() + com_var; }
__asm__(".text\n.weak w_ifunc\n.type w_ifunc, @gnu_indirect_function\n"
        "w_ifunc:\nret\n"
        ".section .data.u_obj,\"aw\"\n"
        ".globl u_obj\n.type u_obj, @gnu_unique_object\n"
        ".size u_obj, 4\nu_obj:\n.long 1\n"
        ".section .bss.u_bss,\"aw\",@nobits\n"
        ".globl u_bss\n.type u_bss, @gnu_unique_object\n"
        ".size u_bss, 4\nu_bss:\n.zero 4\n.text\n");
SRC
    cc -c -fcommon -Wa,--elf-stt-common=yes -o "$TMP/gnuext.o" "$TMP/gnuext.c"

    OUT_X=$($NM "$TMP/gnuext.o")
    expect_match "global IFUNC is i" "$OUT_X" '[[:space:]]i[[:space:]]+g_ifunc$'
    expect_match "local IFUNC is i" "$OUT_X" '[[:space:]]i[[:space:]]+l_ifunc$'
    expect_match "weak IFUNC is i" "$OUT_X" '[[:space:]]i[[:space:]]+w_ifunc$'
    expect_match "unique data is u" "$OUT_X" '[[:space:]]u[[:space:]]+u_obj$'
    expect_match "unique bss is u" "$OUT_X" '[[:space:]]u[[:space:]]+u_bss$'
    expect_match "common is C" "$OUT_X" '[[:space:]]C[[:space:]]+com_var$'

    OUT_XS=$($NM --format=sysv "$TMP/gnuext.o")
    expect_match "sysv types STT_COMMON as COMMON" "$OUT_XS" '^com_var +\|[^|]*\|[^|]*\| *COMMON *\|'
    expect_match "sysv shows IFUNC class i" "$OUT_XS" '^g_ifunc +\|[^|]*\| *i *\|'

    if nm --version 2>/dev/null | grep -q 'GNU nm'; then
        for flags in "" "-g"; do
            # shellcheck disable=SC2086
            GOT=$($NM $flags "$TMP/gnuext.o")
            # shellcheck disable=SC2086
            WANT=$(nm $flags "$TMP/gnuext.o")
            [ "$GOT" = "$WANT" ] || {
                printf 'FAIL: nm %s on IFUNC/unique differs from GNU nm\n--- got:\n%s\n--- want:\n%s\n' \
                    "$flags" "$GOT" "$WANT" >&2
                exit 1
            }
            pass "nm $flags on IFUNC/unique matches GNU nm"
        done
    fi

    # Target-specific special symbols.  GNU nm hides ARM, AArch64 and
    # RISC-V mapping symbols (and, on RISC-V, local labels) unless
    # --special-syms is given; -a does not bring them back, and no
    # other machine has any.  The host assembler cannot target those
    # machines, so an x86 object carrying the names gets its e_machine
    # rewritten: nm only looks at the symbol table here.
    cat > "$TMP/special.s" <<'SRC'
        .data
"$a":   .byte 0
"$t":   .byte 0
"$d":   .byte 0
"$d.1": .byte 0
"$m":   .byte 0
"$f":   .byte 0
"$p.2": .byte 0
"$x":   .byte 0
"$x.foo": .byte 0
"$xrv64i2p1": .byte 0
.Lkeep: .byte 0
plain:  .byte 0
SRC
    cc -m32 -c -Wa,-L -o "$TMP/special32.o" "$TMP/special.s"
    cc -c -Wa,-L -o "$TMP/special64.o" "$TMP/special.s"
    cp "$TMP/special32.o" "$TMP/special_arm.o"
    cp "$TMP/special64.o" "$TMP/special_aarch64.o"
    cp "$TMP/special64.o" "$TMP/special_riscv.o"
    # e_machine is the little-endian half-word at offset 18.
    printf '\050\000' | dd of="$TMP/special_arm.o" bs=1 seek=18 conv=notrunc 2>/dev/null
    printf '\267\000' | dd of="$TMP/special_aarch64.o" bs=1 seek=18 conv=notrunc 2>/dev/null
    printf '\363\000' | dd of="$TMP/special_riscv.o" bs=1 seek=18 conv=notrunc 2>/dev/null

    ALL_SPECIAL='$a $d $d.1 $f $m $p.2 $t $x $x.foo $xrv64i2p1 .Lkeep plain'
    special_names() {
        "$NM" "$@" | awk '{ print $NF }' | grep -v '^\.data$' | tr '\n' ' ' | sed 's/ $//'
    }
    expect_names() {
        label=$1
        want=$2
        shift 2
        got=$(special_names "$@")
        [ "$got" = "$want" ] || {
            printf 'FAIL: %s\n  got:  %s\n  want: %s\n' "$label" "$got" "$want" >&2
            exit 1
        }
        pass "$label"
    }

    expect_names "x86 has no special symbols" "$ALL_SPECIAL" "$TMP/special64.o"
    expect_names "ARM hides \$<letter> mapping symbols" \
        '$xrv64i2p1 .Lkeep plain' "$TMP/special_arm.o"
    expect_names "-a does not show ARM mapping symbols" \
        '$xrv64i2p1 .Lkeep plain' -a "$TMP/special_arm.o"
    expect_names "AArch64 hides \$x/\$d mapping and \$m/\$f/\$p tag symbols" \
        '$a $t $xrv64i2p1 .Lkeep plain' "$TMP/special_aarch64.o"
    expect_names "RISC-V hides mapping symbols and local labels" \
        '$a $d.1 $f $m $p.2 $t $x.foo plain' "$TMP/special_riscv.o"
    for m in arm aarch64 riscv; do
        expect_names "--special-syms shows $m special symbols" \
            "$ALL_SPECIAL" --special-syms "$TMP/special_$m.o"
    done

    # A section symbol has an empty st_name, but BFD names it after its
    # section, so RISC-V's "empty names are special" rule does not hide
    # it: -a lists .text and the .debug_* section symbols there as on
    # any other machine.  The relocations are stripped because nm
    # cannot read x86 relocations under a RISC-V e_machine.
    printf 'int sec_x;\nint sec_f(void) { return sec_x; }\n' > "$TMP/secsym.c"
    cc -g -c -o "$TMP/secsym_rel.o" "$TMP/secsym.c"
    objcopy --remove-relocations='*' "$TMP/secsym_rel.o" "$TMP/secsym.o"
    cp "$TMP/secsym.o" "$TMP/secsym_riscv.o"
    printf '\363\000' | dd of="$TMP/secsym_riscv.o" bs=1 seek=18 conv=notrunc 2>/dev/null
    WANT_SEC=$($NM -a "$TMP/secsym.o")
    printf '%s\n' "$WANT_SEC" | grep -q '[[:space:]]N[[:space:]]' ||
        fail "x86 -a lists debug section symbols (got: $WANT_SEC)"
    GOT_SEC=$($NM -a "$TMP/secsym_riscv.o")
    [ "$GOT_SEC" = "$WANT_SEC" ] || {
        printf 'FAIL: RISC-V -a keeps section symbols\n--- got:\n%s\n--- want:\n%s\n' \
            "$GOT_SEC" "$WANT_SEC" >&2
        exit 1
    }
    pass "RISC-V -a keeps section symbols"
    if nm --version 2>/dev/null | grep -q 'GNU nm'; then
        N_GOT=$(printf '%s\n' "$GOT_SEC" | wc -l)
        N_WANT=$(nm -a "$TMP/secsym_riscv.o" | wc -l)
        [ "$N_GOT" = "$N_WANT" ] ||
            fail "RISC-V -a lists as many symbols as GNU nm ($N_GOT vs $N_WANT)"
        pass "RISC-V -a lists as many symbols as GNU nm"
    fi

    # Command-line parsing as GNU getopt does it: "--" ends the
    # options, --line-numbers is the long -l, and an option argument
    # given as the next word (-t, -f, --format, --radix, bundled or
    # not) is consumed as that argument, never taken for a file.
    cp "$TMP/a.o" "$TMP/-dash.o"
    OUT_DD=$(cd "$TMP" && $NM -- -dash.o) ||
        fail "-- ends the options (got: $OUT_DD)"
    expect_match "-- ends the options" "$OUT_DD" '[[:space:]]T[[:space:]]+alpha$'
    OUT_DD2=$(cd "$TMP" && $NM -- -dash.o b.o)
    expect_match "-- with two files names the first" "$OUT_DD2" '^-dash\.o:$'
    expect_match "-- still lists later files" "$OUT_DD2" '[[:space:]]T[[:space:]]+beta$'
    OUT_DD3=$(cd "$TMP" && $NM -- -g a.o 2>&1) && fail "-- makes -g a file name"
    expect_match "-- makes -g a file name" "$OUT_DD3" '[-]g'
    expect_nomatch "-- makes -g no option" "$OUT_DD3" 'unrecognized'

    OUT_LN=$($NM --line-numbers "$TMP/a.o") ||
        fail "--line-numbers is accepted (got: $OUT_LN)"
    [ "$OUT_LN" = "$($NM -l "$TMP/a.o")" ] || fail "--line-numbers matches -l"
    pass "--line-numbers matches -l"

    same_output() {
        label=$1
        want=$2
        shift 2
        got=$("$NM" "$@" 2>&1) || {
            printf 'FAIL: %s (exit status)\n%s\n' "$label" "$got" >&2
            exit 1
        }
        [ "$got" = "$want" ] || {
            printf 'FAIL: %s\n--- got:\n%s\n--- want:\n%s\n' "$label" "$got" "$want" >&2
            exit 1
        }
        pass "$label"
    }
    WANT_TD=$($NM -a -t d "$TMP/a.o" "$TMP/b.o")
    same_output "-at d takes d as the radix" "$WANT_TD" -at d "$TMP/a.o" "$TMP/b.o"
    same_output "--radix d takes d as the radix" "$WANT_TD" -a --radix d "$TMP/a.o" "$TMP/b.o"
    WANT_FS=$($NM -a --format=sysv "$TMP/a.o")
    same_output "-af sysv takes sysv as the format" "$WANT_FS" -af sysv "$TMP/a.o"
    same_output "-f sysv takes sysv as the format" "$WANT_FS" -a -f sysv "$TMP/a.o"
    same_output "-fsysv takes sysv as the format" "$WANT_FS" -a -fsysv "$TMP/a.o"
    same_output "--format sysv takes sysv as the format" "$WANT_FS" -a --format sysv "$TMP/a.o"
    WANT_FP=$($NM -P "$TMP/a.o" "$TMP/b.o")
    same_output "-f posix with two files" "$WANT_FP" -f posix "$TMP/a.o" "$TMP/b.o"

    if nm --version 2>/dev/null | grep -q 'GNU nm'; then
        GOT=$(cd "$TMP" && $NM -- -dash.o b.o)
        WANT=$(cd "$TMP" && nm -- -dash.o b.o)
        [ "$GOT" = "$WANT" ] || {
            printf 'FAIL: nm -- differs from GNU nm\n--- got:\n%s\n--- want:\n%s\n' \
                "$GOT" "$WANT" >&2
            exit 1
        }
        pass "nm -- matches GNU nm"
        for flags in "-gt x" "-g --radix x" "-gf bsd" "-g -f bsd" "-g --format bsd"; do
            # shellcheck disable=SC2086
            GOT=$($NM $flags "$TMP/a.o" "$TMP/b.o")
            # shellcheck disable=SC2086
            WANT=$(nm $flags "$TMP/a.o" "$TMP/b.o")
            [ "$GOT" = "$WANT" ] || {
                printf 'FAIL: nm %s differs from GNU nm\n--- got:\n%s\n--- want:\n%s\n' \
                    "$flags" "$GOT" "$WANT" >&2
                exit 1
            }
            pass "nm $flags matches GNU nm"
        done
    fi

    if [ -x /bin/ls ]; then
        OUT_DYN=$($NM -D /bin/ls)
        expect_match "-D dynamic symbols produce output" "$OUT_DYN" '^[[:space:]]*[0-9a-fA-F]*[[:space:]]+[A-Za-z][[:space:]]+'
    fi

    pass "nm smoke tests passed"
}

main "$@"
