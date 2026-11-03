#!/bin/sh
# Version scripts: --version-script and a linker script's VERSION { }.
#
# The linker read over a script's VERSION and did not know
# --version-script: Linux's vDSO exported every global, without the
# LINUX_2.6 version glibc looks its functions up by, and had no
# .gnu.version_d.  GNU ld gives each defined global a node's global
# patterns match that node's version (the default one), makes one a local
# pattern matches local, defines every named node (with the node it
# depends on), and a symbol of each version's name.
#
# GNU ld is the judge: a shared object linked with --version-script (two
# nodes, one depending on the other, exact names, a pattern, local: *, a
# node no symbol is given) and one linked through a script's VERSION, as
# the vDSO is, have GNU ld's dynamic symbols with their versions, GNU
# ld's version definitions, and the hidden symbols local in .symtab.
set -u

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
fail=0

if ! ${GNU_LD:-ld} --version 2>/dev/null | grep -q 'GNU ld'; then
    echo "skip: GNU ld is the judge"
    exit 0
fi
${CC:-cc} -O0 -w -o "$work/ld" \
    -idirafter "$top/include" -idirafter "$top/sys" -idirafter "$top/sys/include" \
    -I"$top/usr.lib/elfobj/src" "$top"/usr.bin/ld/*.c "$top"/usr.lib/elfobj/src/*.c || {
    echo "FAIL: the linker does not build for the host"; exit 1; }

cd "$work" || exit 1
cat > lib.c <<'EOF'
int f(void) { return 1; }
int g_one(void) { return 2; }
int g_two(void) { return 3; }
int h(void) { return f() + 4; }
int internal(void) { return 5; }
int counter = 6;
EOF
${GCC:-gcc} -O2 -fPIC -c -o lib.o lib.c || exit 1
cat > lib.map <<'EOF'
/* a comment */
VERS_1 {
    global:
        f; g_*;
        counter;
    local: *;
};
VERS_2 {
    global: h;
} VERS_1;
VERS_3 { };
EOF

dynsyms() {
    readelf --dyn-syms -W "$1" | awk 'NR > 3 && $8 != "" { print $4, $5, ($7 == "UND" ? "UND" : ($7 == "ABS" ? "ABS" : "DEF")), $8 }' | sort
}
verdefs() {
    readelf -VW "$1" | sed -n '/Version definition/,/^$/p' | grep -E 'Rev:|Parent' |
        sed -e 's/^ *0x[0-9a-f]*: *//' -e 's/^ *//'
}
locals() {
    readelf -sW "$1" | sed -n '/Symbol table .\.symtab/,$p' | awk '$5 == "LOCAL" && $8 ~ /^(internal|f|h|g_one)$/ { print $8 }' | sort
}
compare() {     # WHAT
    for what in dynsyms verdefs locals; do
        $what gnu.so > want
        $what ours.so > have
        if [ ! -s want ] && [ $what != locals ]; then
            echo "FAIL: $1: GNU ld's has no $what"; fail=1
        elif ! cmp -s want have; then
            echo "FAIL: $1: $what differ from GNU ld's:"; diff want have | sed 's/^/    /'; fail=1
        fi
    done
}

"${GNU_LD:-ld}" -shared -soname libt.so.1 --version-script=lib.map -o gnu.so lib.o || exit 1
if ! ./ld -m elf_x86_64 -shared -soname libt.so.1 --version-script lib.map -o ours.so lib.o > said 2>&1; then
    echo "FAIL: ld --version-script: $(head -1 said)"; fail=1
else
    compare "--version-script"
fi

# a script's VERSION, the vDSO's way
cat > vdso.c <<'EOF'
long __vdso_clock(int w) { return w + 1; }
long __vdso_time(long *t) { return t ? *t : 0; }
long clock(int w) __attribute__((weak, alias("__vdso_clock")));
long helper(long v) { return v * 2; }
EOF
${GCC:-gcc} -O2 -fPIC -c -o vdso.o vdso.c || exit 1
cat > vdso.lds <<'EOF'
SECTIONS
{
	. = SIZEOF_HEADERS;
	.hash : { *(.hash) } :text
	.gnu.hash : { *(.gnu.hash) }
	.dynsym : { *(.dynsym) }
	.dynstr : { *(.dynstr) }
	.gnu.version : { *(.gnu.version) }
	.gnu.version_d : { *(.gnu.version_d) }
	.dynamic : { *(.dynamic) } :text :dynamic
	.text : { *(.text*) } :text
}
PHDRS
{
	text PT_LOAD FLAGS(5) FILEHDR PHDRS;
	dynamic PT_DYNAMIC FLAGS(4);
}
VERSION {
	LINUX_2.6 {
	global:
		clock;
		__vdso_*;
	local: *;
	};
}
EOF
flags="-shared --hash-style=both --no-undefined -Bsymbolic -soname linux-vdso.so.1 -T vdso.lds"
# shellcheck disable=SC2086
"${GNU_LD:-ld}" $flags -o gnu.so vdso.o || exit 1
# shellcheck disable=SC2086
if ! ./ld -m elf_x86_64 $flags -o ours.so vdso.o > said 2>&1; then
    echo "FAIL: ld -T with VERSION: $(head -1 said)"; fail=1
else
    compare "a script's VERSION"
fi

# A pattern the script's lexer splits, "ns::fn" (':' does not go in a
# name), must be put back whole: the VERSION text was rebuilt with a space
# between every two tokens, so it read as the patterns "ns" and "fn", and
# the symbol fell to local: *.
cat > colon.s <<'EOF'
	.text
	.globl	"ns::fn", plain, hidden
	.type	"ns::fn", @function
	.type	plain, @function
	.type	hidden, @function
"ns::fn":
plain:
hidden:
	ret
EOF
${GCC:-gcc} -c -o colon.o colon.s || exit 1
cat > colon.lds <<'EOF'
VERSION {
	COLON_1 {
	global:
		ns::fn;
		plain;
	local: *;
	};
}
EOF
"${GNU_LD:-ld}" -shared -soname libc1.so -T colon.lds -o gnu.so colon.o 2>/dev/null || exit 1
if ! ./ld -m elf_x86_64 -shared -soname libc1.so -T colon.lds -o ours.so colon.o > said 2>&1; then
    echo "FAIL: ld -T with VERSION ns::fn: $(head -1 said)"; fail=1
else
    if ! dynsyms gnu.so | grep -q 'ns::fn'; then
        echo "FAIL: GNU ld did not export ns::fn"; fail=1
    fi
    compare "a script's VERSION with ns::fn"
fi

if [ $fail -eq 0 ]; then
    echo "ok   test_version_script"
fi
exit $fail
