#!/bin/sh
# Sections ordered by another (SHF_LINK_ORDER) under -r and in a program.
#
# gcc -fpatchable-function-entry puts each function's entry in a
# __patchable_function_entries section ordered by the function's own
# section.  The linker merged every one of them into a single section
# whose sh_link was 0, ordered by nothing; GNU ld keeps one for each
# output section they are ordered by, its sh_link that section.
#
# GNU ld is the judge: under -r, with and without -ffunction-sections,
# the link-order sections are the ones GNU ld makes (ordered by the
# same sections, of the same sizes); each entry, once GNU ld links the
# -r output into a program, still names the function it did; and in a
# program the section is ordered by .text, as GNU ld's is.
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
if ! echo 'void f(void){}' | ${GCC:-gcc} -x c -fpatchable-function-entry=2 -c -o /dev/null - 2>/dev/null; then
    echo "skip: no gcc with -fpatchable-function-entry"
    exit 0
fi
${CC:-cc} -O0 -w -o "$work/ld" \
    -idirafter "$top/include" -idirafter "$top/sys" -idirafter "$top/sys/include" \
    -I"$top/usr.lib/elfobj/src" "$top"/usr.bin/ld/*.c "$top"/usr.lib/elfobj/src/*.c || {
    echo "FAIL: the linker does not build for the host"; exit 1; }

cd "$work" || exit 1
cat > a.c <<'EOF'
void f(void) {}
void g(void) {}
void _start(void) {}
EOF
cat > b.c <<'EOF'
void h(void) {}
EOF

# Each link-order section of OBJ: its name, the name of the section it is
# ordered by, and its size, one to a line, sorted.
ordered() {
    readelf -SW "$1" | sed -n 's/^ *\[ *\([0-9]*\)\] /\1 /p' | awk '
        { idx = $1; name[idx] = $2; flg = ""; lk = ""; sz = ""
          # [Nr] Name Type Address Off Size ES Flg Lk Inf Al
          n = NF; al = $n; inf = $(n-1); lk = $(n-2); flg = $(n-3); sz = $6
          link[idx] = lk; flags[idx] = flg; size[idx] = sz }
        END { for (i in name) if (flags[i] ~ /L/) print name[i], name[link[i]], size[i] }' | sort
}

# Where each entry of a program's __patchable_function_entries points,
# as the function it is in and the distance into it, sorted.
entries() {
    nm "$1" | awk '$2 ~ /^[Tt]$/ { print $1, $3 }' | sort > syms
    objcopy -O binary --only-section=__patchable_function_entries "$1" pfe.bin 2>/dev/null || return 1
    od -An -v -t x8 pfe.bin | tr -s ' ' '\n' | grep . | while read -r v; do
        awk -v v="$v" 'function h(s,  i, c, n) { n = 0; s = tolower(s)
                for (i = 1; i <= length(s); i++) { c = index("0123456789abcdef", substr(s, i, 1)) - 1; n = n * 16 + c }
                return n }
            BEGIN { t = h(v) } { a = h($1); if (a <= t && a > best) { best = a; name = $2 } }
            END { print name "+" (t - best) }' syms
    done | sort
}

for opt in "" -ffunction-sections; do
    ${GCC:-gcc} -O2 -fpatchable-function-entry=2 $opt -c -o a.o a.c || exit 1
    ${GCC:-gcc} -O2 -fpatchable-function-entry=2 $opt -c -o b.o b.c || exit 1

    "${GNU_LD:-ld}" -r -o gnu.o a.o b.o || exit 1
    if ! ./ld -m elf_x86_64 -r -o ours.o a.o b.o > said 2>&1; then
        echo "FAIL: ld -r $opt: $(head -1 said)"; fail=1; continue
    fi
    ordered gnu.o > want
    ordered ours.o > have
    if ! cmp -s want have; then
        echo "FAIL: ld -r $opt: the link-order sections are"
        sed 's/^/    /' have
        echo "  GNU ld's:"
        sed 's/^/    /' want
        fail=1
    fi

    # what the entries name, once the -r output is linked by GNU ld
    "${GNU_LD:-ld}" -o gnu.out gnu.o || exit 1
    "${GNU_LD:-ld}" -o ours.out ours.o || exit 1
    entries gnu.out > want
    entries ours.out > have
    if [ "$(wc -l < want)" -ne 4 ]; then
        echo "FAIL: GNU ld's program has $(wc -l < want) entries, not one for each of 4 functions"
        fail=1
    elif ! cmp -s want have; then
        echo "FAIL: ld -r $opt: the entries name $(tr '\n' ' ' < have), GNU ld's $(tr '\n' ' ' < want)"
        fail=1
    fi

    # in a program: ordered by .text
    "${GNU_LD:-ld}" -o gnu.out a.o b.o || exit 1
    if ! ./ld -m elf_x86_64 -o ours.out a.o b.o > said 2>&1; then
        echo "FAIL: ld $opt: $(head -1 said)"; fail=1; continue
    fi
    ordered gnu.out > want
    ordered ours.out > have
    if ! cmp -s want have; then
        echo "FAIL: ld $opt: the link-order sections of the program are $(tr '\n' ' ' < have)," \
            "GNU ld's $(tr '\n' ' ' < want)"
        fail=1
    fi
done

if [ $fail -eq 0 ]; then
    echo "ok   test_link_order"
fi
exit $fail
