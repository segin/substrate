#!/bin/sh
# What the linker says of an executable stack and of an RWX segment, and
# the options that choose it: --[no-]warn-execstack,
# --warn-execstack-objects, --[no-]error-execstack,
# --[no-]warn-rwx-segments, --[no-]error-rwx-segments.
#
# The linker knew none of them.  It said "unsupported option ignored" for
# each (the Linux build passes --no-warn-rwx-segments to every link), and
# warned of neither thing, which GNU ld does by default.
#
# GNU ld is the judge.  For each command line, both linkers are run over
# an output that already exists, and must agree on whether the link
# succeeds, whether that output is still there afterwards (a failed link
# takes it away), and what is said of the stack and the segments: the
# same warnings and errors, word for word, after each linker's name.
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
if ! ${GNU_LD:-ld} --help 2>/dev/null | grep -q -- '--warn-rwx-segments'; then
    echo "skip: GNU ld is older than these options"
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
	.data
	.long 1
EOF
# an input that asks for an executable stack
cat > x.s <<'EOF'
	.section .note.GNU-stack,"x",@progbits
EOF
cat > rwx.ld <<'EOF'
PHDRS { all PT_LOAD FLAGS(7); }
SECTIONS { . = 0x400000; .text : { *(.text) } :all .data : { *(.data) } :all }
EOF
for m in 64 32; do
    ${AS_GNU:-as} --$m -o a$m.o a.s || exit 1
    ${AS_GNU:-as} --$m -o x$m.o x.s || exit 1
    cp x$m.o y$m.o
done

# What a linker says of these things, its own name taken off the front:
# ld, or as Ubuntu's GNU ld calls itself, /usr/bin/x86_64-linux-gnu-ld.bfd.
said() {
    grep -E 'executable stack|RWX permissions' "$1" | sed -E 's/^[^ ]*ld(\.[a-z]+)?: //'
}

# run LINKER RESULT ARGS...: the link over an output from before; RESULT
# gets its status, whether the output is left, and what was said.
run() {
    linker=$1; result=$2; shift 2
    echo old > out
    "$linker" "$@" > err 2>&1
    rc=$?
    { [ $rc -eq 0 ] && echo "links" || echo "fails"
      [ -f out ] && echo "output left" || echo "no output"
      said err; } > "$result"
}

# check M ARGS...: GNU ld and this one agree, for -m elf_x86_64 or elf_i386
check() {
    m=$1; shift
    if [ "$m" = 64 ]; then em=elf_x86_64; else em=elf_i386; fi
    args=$(echo "$*" | sed "s/@/$m/g")
    # shellcheck disable=SC2086
    run "${GNU_LD:-ld}" want -m $em -o out $args
    # shellcheck disable=SC2086
    run ./ld have -m $em -o out $args
    if ! cmp -s want have; then
        echo "FAIL: ld -m $em $args:"
        echo "  GNU ld: $(tr '\n' '|' < want)"
        echo "  ours:   $(tr '\n' '|' < have)"
        fail=1
    fi
}

for m in 64 32; do
    # an executable stack an input asks for
    check $m a@.o
    check $m x@.o a@.o
    check $m a@.o x@.o y@.o
    check $m -r x@.o a@.o
    check $m x@.o a@.o --warn-execstack-objects
    check $m x@.o a@.o --warn-execstack
    check $m x@.o a@.o --no-warn-execstack
    check $m x@.o a@.o --no-warn-execstack --warn-execstack-objects
    check $m x@.o a@.o --error-execstack
    check $m x@.o a@.o --error-execstack --no-error-execstack
    check $m -r x@.o a@.o --error-execstack
    check $m x@.o a@.o --fatal-warnings
    check $m -shared x@.o a@.o
    # one -z decided
    check $m -z execstack x@.o a@.o
    check $m -z noexecstack x@.o a@.o --error-execstack
    check $m -z execstack a@.o --warn-execstack
    check $m -z execstack a@.o --warn-execstack --no-warn-execstack
    check $m -z execstack a@.o --error-execstack
    check $m -z execstack a@.o --warn-execstack --error-execstack
    # a segment both writable and executable
    check $m -T rwx.ld a@.o
    check $m -T rwx.ld a@.o --warn-rwx-segments
    check $m -T rwx.ld a@.o --no-warn-rwx-segments
    check $m -T rwx.ld a@.o --error-rwx-segments
    check $m -T rwx.ld a@.o --error-rwx-segments --no-error-rwx-segments
    check $m -T rwx.ld a@.o --no-warn-rwx-segments --error-rwx-segments
    check $m -T rwx.ld a@.o --fatal-warnings
done

if [ $fail -eq 0 ]; then
    echo "ok   test_execstack_rwx"
fi
exit $fail
