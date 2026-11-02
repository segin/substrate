#!/bin/sh
# --build-id: the .note.gnu.build-id note.
#
# The linker took --build-id and made no note: Linux's vDSO and vmlinux,
# linked with --build-id=sha1, had no build ID, and their PT_NOTE was 0x24
# bytes short of GNU ld's.  GNU ld makes an NT_GNU_BUILD_ID note, owner
# "GNU", of 20 bytes for sha1 (the default), 16 for md5 and uuid, the
# given bytes for 0xHEX, none for none.
#
# GNU ld is the judge of the note's section (name, type, alignment, size)
# and header, for each style, and of where a script puts it.  The sha1 and
# md5 are of the output, which is not GNU ld's to the byte, so each is
# checked against the digest of the file with the ID zeroed, as it was
# taken; and the same link gives the same ID, uuid a new one each time.
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
printf 'int f(void) { return 1; }\nint g(void) { return f() + 1; }\n' > a.c
${GCC:-gcc} -O2 -fPIC -c -o a.o a.c || exit 1

# the build-id section: name, type, flags, alignment, size
section() {
    readelf -SW "$1" | sed -n 's/^ *\[ *[0-9]*\] //p' | awk '$1 == ".note.gnu.build-id" { print $1, $2, $5, $(NF) }'
}
# the note's header: owner, descriptor size, type
# (readelf's warnings of GNU ld's notes, below, are not ours to show)
header() {
    readelf -nW "$1" 2>/dev/null | awk '/NT_GNU_BUILD_ID/ { print $1, $2, $3 }'
}
id() {
    readelf -nW "$1" 2>/dev/null | sed -n 's/.*Build ID: *\([0-9a-f]*\).*/\1/p'
}
# the digest (sha1sum or md5sum) of OBJ with its ID zeroed
digest() {
    cp "$2" zeroed
    off=$(readelf -SW "$2" | sed -n 's/^ *\[ *[0-9]*\] //p' | awk '$1 == ".note.gnu.build-id" { print $4 }')
    n=$(( ${#3} / 2 ))
    dd if=/dev/zero of=zeroed bs=1 seek=$(( 0x$off + 16 )) count=$n conv=notrunc 2>/dev/null
    $1 zeroed | cut -d' ' -f1
}

for style in "" =sha1 =md5 =uuid =0x0123456789abcdef =0xdead-beef =none; do
    "${GNU_LD:-ld}" -shared --build-id$style -o gnu.so a.o || exit 1
    if ! ./ld -m elf_x86_64 -shared --build-id$style -o ours.so a.o > said 2>&1; then
        echo "FAIL: ld --build-id$style: $(head -1 said)"; fail=1; continue
    fi
    if [ "$(section gnu.so)" != "$(section ours.so)" ] || [ "$(header gnu.so)" != "$(header ours.so)" ]; then
        echo "FAIL: --build-id$style: [$(section ours.so) | $(header ours.so)], GNU ld's [$(section gnu.so) | $(header gnu.so)]"
        fail=1
        continue
    fi
    case $style in
        ""|=sha1) want=$(digest sha1sum ours.so "$(id ours.so)") ;;
        =md5) want=$(digest md5sum ours.so "$(id ours.so)") ;;
        =0x*) want=$(id gnu.so) ;;
        *) want=$(id ours.so) ;;
    esac
    if [ "$(id ours.so)" != "$want" ]; then
        echo "FAIL: --build-id$style: the ID is $(id ours.so), not $want"; fail=1
    fi
    ./ld -m elf_x86_64 -shared --build-id$style -o again.so a.o
    if [ "$style" = =uuid ]; then
        [ "$(id again.so)" != "$(id ours.so)" ] || { echo "FAIL: --build-id=uuid gives the same ID twice"; fail=1; }
    elif ! cmp -s again.so ours.so; then
        echo "FAIL: --build-id$style: the same link gives another output"; fail=1
    fi
done
# the default is no build ID, as GNU ld's
./ld -m elf_x86_64 -shared -o plain.so a.o
if [ -n "$(section plain.so)" ]; then
    echo "FAIL: a build ID without --build-id"; fail=1
fi

# a script that gathers the notes, as the vDSO's: the note goes in .note, PT_NOTE as GNU ld's
cat > notes.lds <<'EOF'
SECTIONS
{
	. = SIZEOF_HEADERS;
	.hash : { *(.hash) } :text
	.gnu.hash : { *(.gnu.hash) }
	.dynsym : { *(.dynsym) }
	.dynstr : { *(.dynstr) }
	.gnu.version : { *(.gnu.version) }
	.dynamic : { *(.dynamic) } :text :dynamic
	.note : { *(.note*) } :text :note
	.text : { *(.text*) } :text
}
PHDRS
{
	text PT_LOAD FLAGS(5) FILEHDR PHDRS;
	dynamic PT_DYNAMIC FLAGS(4);
	note PT_NOTE FLAGS(4);
}
EOF
# (an object without the property notes, which GNU ld itself puts in one section with the 4-aligned
# build ID badly, 8-aligned: readelf warns of its .note)
${GCC:-gcc} -O2 -fPIC -fcf-protection=none -Wa,-mx86-used-note=no -c -o bare.o a.c || exit 1
"${GNU_LD:-ld}" -shared --build-id=sha1 -T notes.lds -o gnu.so bare.o || exit 1
if ! ./ld -m elf_x86_64 -shared --build-id=sha1 -T notes.lds -o ours.so bare.o > said 2>&1; then
    echo "FAIL: ld -T --build-id: $(head -1 said)"; fail=1
else
    # the section the note is found in (where the orphans after it go is another matter)
    in_sec() { readelf -nW "$1" 2>/dev/null | grep -B3 NT_GNU_BUILD_ID | sed -n 's/.*notes found in: *//p'; }
    if [ "$(in_sec gnu.so)" != "$(in_sec ours.so)" ] || [ "$(header gnu.so)" != "$(header ours.so)" ]; then
        echo "FAIL: -T: the note is in [$(in_sec ours.so)], GNU ld's in [$(in_sec gnu.so)]"; fail=1
    fi
fi

if [ $fail -eq 0 ]; then
    echo "ok   test_build_id"
fi
exit $fail
