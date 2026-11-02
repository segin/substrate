#!/bin/sh
# -z separate-code and -z noseparate-code, and GNU ld's default for them.
#
# The linker put the headers, and what comes before the code (.interp,
# the dynamic linker's tables), in an R E first segment with the code,
# and the read-only data after it.  GNU ld for x86 Linux defaults to
# -z separate-code: R (the headers, .interp, .dynsym and the like), R E
# (the code, on pages of its own), R (.rodata, .eh_frame), RW; with
# -z noseparate-code the headers, the code and the read-only data are
# one R E segment.  For a target that is not Linux (Substrate) the
# default is -z noseparate-code, as GNU ld's.
#
# GNU ld is the judge: for a program, a PIE and a shared object, x86-64
# and i386, either option and neither, the PT_LOADs are GNU ld's in
# number and permissions, each allocated section is in a segment of the
# same permissions as in GNU ld's, no page holds both code and anything
# else, and a program linked by gcc through this linker runs.
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
cat > a.s <<'EOF'
	.text
	.globl _start
_start:	ret
	.section .rodata
	.long 1
	.data
	.long 2
EOF
${AS_GNU:-as} --64 -o a64.o a.s || exit 1
${AS_GNU:-as} --32 -o a32.o a.s || exit 1

# The PT_LOADs of OBJ, by their permissions, in order.
loads() {
    readelf -lW "$1" | awk '$1 == "LOAD" { printf "%s ", $7 $8 } END { print "" }' | sed 's/0x[0-9a-f]*//g'
}
# Each allocated section of OBJ and the permissions of the PT_LOAD it is in.
placement() {
    readelf -lW "$1" | awk '
        $1 ~ /^(LOAD|PHDR|INTERP|DYNAMIC|NOTE|GNU_[A-Z_]*|TLS)$/ { t[n] = $1; f[n] = $7 $8; sub(/0x.*/, "", f[n]); n++ }
        /Section to Segment/ { s = 1; next }
        s && $1 ~ /^[0-9]+$/ { i = $1 + 0; if (t[i] == "LOAD") for (k = 2; k <= NF; k++) print $k, f[i] }' | sort
}
# Whether a page of OBJ holds code and anything else: the R E PT_LOADs
# begin and end on pages no other PT_LOAD has.
code_alone() {
    readelf -lW "$1" | awk '$1 == "LOAD" {
        v = strtonum($3); m = strtonum($6); f = $7 $8
        lo[n] = int(v / 4096); hi[n] = int((v + m - 1) / 4096); x[n] = (f ~ /E/); n++ }
        END { for (i = 0; i < n; i++) for (j = 0; j < n; j++)
                  if (i != j && x[i] && !x[j] && lo[j] <= hi[i] && hi[j] >= lo[i]) { print "shared"; exit }
              print "alone" }'
}

# check M OBJ ARGS...: GNU ld and this one, -m M
check() {
    m=$1; obj=$2; shift 2
    "${GNU_LD:-ld}" -m "$m" "$@" -o gnu.out "$obj" 2>/dev/null || { echo "skip: GNU ld -m $m $*"; return; }
    if ! ./ld -m "$m" "$@" -o ours.out "$obj" > said 2>&1; then
        echo "FAIL: ld -m $m $*: $(head -1 said)"; fail=1; return
    fi
    if [ "$(loads gnu.out)" != "$(loads ours.out)" ]; then
        echo "FAIL: ld -m $m $*: PT_LOADs [$(loads ours.out)], GNU ld's [$(loads gnu.out)]"; fail=1
    fi
    placement gnu.out | grep -v -e '\.note\.substrate_ld' > want
    placement ours.out | grep -v -e '\.note\.substrate_ld' > have
    # the sections both have, in segments of the same permissions
    join want have | awk '$2 != $3 { print }' > moved
    if [ -s moved ]; then
        echo "FAIL: ld -m $m $*: sections in segments of other permissions (section, GNU ld's, ours):"
        sed 's/^/    /' moved
        fail=1
    fi
    if [ "$(code_alone gnu.out)" = alone ] && [ "$(code_alone ours.out)" != alone ]; then
        echo "FAIL: ld -m $m $*: code shares a page with what is not code"; fail=1
    fi
    if [ "$(rw_shape gnu.out)" != "$(rw_shape ours.out)" ]; then
        echo "FAIL: ld -m $m $*: the writable segment is [$(rw_shape ours.out)], GNU ld's [$(rw_shape gnu.out)]"
        fail=1
    fi
}
# How the writable segment of OBJ sits: whether it is at the same place in
# the page in the file as in memory, whether it shares the file's page
# with the segment before it, whether it begins a page of its own in
# memory, and whether the RELRO part ends on a page.
rw_shape() {
    readelf -lW "$1" | awk '
        $1 == "LOAD" { o = strtonum($2); v = strtonum($3); f = strtonum($5); m = strtonum($6)
                       if ($7 $8 ~ /W/) { ro = o; rv = v; seen = 1 } else if (!seen) { pe = o + f; pv = v + m } }
        $1 == "GNU_RELRO" { re = strtonum($3) + strtonum($6); hasrelro = 1 }
        END { printf "congruent=%d shares-file-page=%d own-memory-page=%d relro-ends-on-page=%s\n",
                  (ro % 4096) == (rv % 4096), int((pe - 1) / 4096) == int(ro / 4096),
                  int((pv - 1) / 4096) != int(rv / 4096), hasrelro ? ((re % 4096) == 0) : "none" }'
}

for z in "" "-z separate-code" "-z noseparate-code"; do
    # shellcheck disable=SC2086
    check elf_x86_64 a64.o $z
    # shellcheck disable=SC2086
    check elf_i386 a32.o $z
    # shellcheck disable=SC2086
    check elf_x86_64 a64.o -pie $z
    # shellcheck disable=SC2086
    check elf_x86_64 a64.o -shared $z
done

# a program of the host's, through gcc, for its dynamic tables and .interp
printf '#include <stdio.h>\nint main(void) { puts("separate"); return 0; }\n' > p.c
if ${GCC:-gcc} -O2 -c -o p.o p.c 2>/dev/null; then
    mkdir -p bin && ln -sf "$work/ld" bin/ld
    for z in "" -Wl,-z,noseparate-code; do
        ${GCC:-gcc} $z -o gnu.out p.o || continue
        if ! ${GCC:-gcc} -B"$work/bin/" $z -o ours.out p.o > said 2>&1; then
            echo "FAIL: gcc -B $z: $(head -1 said)"; fail=1; continue
        fi
        if [ "$(loads gnu.out)" != "$(loads ours.out)" ]; then
            echo "FAIL: gcc -B $z: PT_LOADs [$(loads ours.out)], GNU ld's [$(loads gnu.out)]"; fail=1
        fi
        if [ "$(./ours.out)" != separate ]; then
            echo "FAIL: gcc -B $z: the program does not run"; fail=1
        fi
    done
fi

if [ $fail -eq 0 ]; then
    echo "ok   test_separate_code"
fi
exit $fail
