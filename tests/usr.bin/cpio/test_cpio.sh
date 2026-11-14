#!/usr/bin/env bash
# cpio(1), case by case: exact standard output, standard error and exit
# status, with GNU cpio as the judge where POSIX is silent.  GNU cpio, when
# installed, is also the other end of the interoperability cases.
set -u
ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CPIO=${CPIO:-"$ROOT/usr.bin/cpio/cpio"}
TMP=$(mktemp -d)
trap 'chmod -R u+w "$TMP" 2>/dev/null; rm -rf "$TMP"' EXIT
cd "$TMP" || exit 1
export TZ=UTC LC_ALL=C
umask 022

GNU=
if command -v cpio >/dev/null 2>&1 && cpio --version 2>/dev/null | grep -q GNU; then
    GNU=$(command -v cpio)
fi

fail=0
cases=0

# run CMD...: run it in a subshell, keeping stdout, stderr and status.
run() {
    "$@" >"$TMP/.out" 2>"$TMP/.err"
    rc=$?
    out=$(cat "$TMP/.out")
    err=$(cat "$TMP/.err")
}

# ok LABEL COND...: COND (a test(1) expression or command) holds.
ok() {
    label=$1; shift
    cases=$((cases + 1))
    if ! "$@"; then
        echo "FAIL $label"
        fail=1
    fi
}

# same LABEL WANT GOT
same() {
    cases=$((cases + 1))
    if [ "$2" != "$3" ]; then
        printf 'FAIL %s\n  want: %s\n  got:  %s\n' "$1" "$2" "$3"
        fail=1
    fi
}

# fresh DIR: an empty directory
fresh() { rm -rf "$1"; mkdir -p "$1"; }

# ---- source tree used by most cases
mkdir src src/d
printf 'hello\n' > src/f
printf 'x\n' > src/d/g

# ---- diagnostics
run "$CPIO" -x
same "unknown option" "cpio: unknown option -x" "$(head -1 .err)"
same "unknown option status" 2 "$rc"
run "$CPIO" -o -H
same "missing argument" "cpio: option -H needs an argument" "$(head -1 .err)"
run "$CPIO" -o -H zip </dev/null
same "unknown format" "cpio: unknown archive format: zip" "$err"
run "$CPIO" -i -F nonexistent.cpio
same "missing archive" "cpio: nonexistent.cpio: No such file or directory" "$err"
same "missing archive status" 2 "$rc"
printf 'src/f\n' | "$CPIO" -o -F v.cpio
fresh xv
run sh -c "cd xv && \"$CPIO\" -idv -F ../v.cpio"
same "-iv names the member, not ./member" "src/f" "$err"

# ---- copy-in from a pipe, the format found without seeking
for fmt in newc odc bin; do
    printf 'src/f\n' | "$CPIO" -o -H "$fmt" -F "p-$fmt.cpio"
    fresh xp
    run sh -c "cat p-$fmt.cpio | (cd xp && \"$CPIO\" -id)"
    same "pipe, $fmt: status" 0 "$rc"
    same "pipe, $fmt: data" "hello" "$(cat xp/src/f 2>&1)"
done

# ---- a member skipped (here as unsafe) leaves the stream in step: its
# data and padding are passed over, and the next member extracts
mkdir -p skip/in
printf 'abc' > skip/up
(cd skip/in && printf '../up\n' | "$CPIO" -o -H newc) > skip1.cpio
printf 'src/f\n' | "$CPIO" -o -H newc > skip2.cpio
# the first archive without its trailer, then the second
head -c "$(( $(grep -abo 'TRAILER!!!' skip1.cpio | head -1 | cut -d: -f1) - 110 ))" skip1.cpio > skip.cpio
cat skip2.cpio >> skip.cpio
fresh xs
run sh -c "cd xs && \"$CPIO\" -id < ../skip.cpio"
same "member after a skipped one" "hello" "$(cat xs/src/f 2>&1)"

# ---- damaged archives are diagnosed, as GNU cpio does
printf 'src/f\n' | "$CPIO" -o -H newc > good.cpio
# xin ARCHIVE: copy it in, in a fresh directory
xin() { fresh xd; run sh -c "cd xd && \"$CPIO\" -id < \"$TMP/$1\""; }
: > empty.cpio
xin empty.cpio
same "empty: message" "cpio: premature end of archive" "$err"
same "empty: status" 2 "$rc"
printf 'garbage garbage\n' > text.cpio
xin text.cpio
same "text: message" "cpio: premature end of archive" "$(tail -1 .err)"
same "text: status" 2 "$rc"
head -c 100 good.cpio > hdr.cpio
xin hdr.cpio
same "cut in a header" "cpio: premature end of file" "$err"
same "cut in a header: status" 2 "$rc"
# newc: header and "src/f" are 116 bytes, the data 116-121, padding to
# 124, then the trailer
head -c 119 good.cpio > data.cpio
xin data.cpio
same "cut in data" "cpio: premature end of file" "$err"
same "cut in data: status" 2 "$rc"
head -c 127 good.cpio > cuttrailer.cpio
xin cuttrailer.cpio
same "cut in the trailer" "cpio: premature end of file" "$err"
head -c 124 good.cpio > notrailer.cpio
xin notrailer.cpio
same "no trailer" "cpio: premature end of file" "$err"
same "no trailer: data kept" "hello" "$(cat xd/src/f 2>&1)"
(printf 'JUNKJUNK'; cat good.cpio) > junk.cpio
xin junk.cpio
same "junk: warning" "cpio: warning: skipped 8 bytes of junk" "$err"
same "junk: status" 0 "$rc"
same "junk: data" "hello" "$(cat xd/src/f 2>&1)"
# a header whose numbers are not hex: diagnosed, skipped, and the next
# member still extracts
(head -c 6 good.cpio; printf 'zzzzzzzz'; head -c 124 good.cpio | tail -c +15; cat good.cpio) > badnum.cpio
xin badnum.cpio
same "bad number" "cpio: malformed header: bad number zzzzzzzz" "$(head -1 .err)"
same "bad number: next member" "hello" "$(cat xd/src/f 2>&1)"

# ---- -t lists names; -tv lines as GNU cpio's
mkdir lt
printf 'hello\n' > lt/f
ln -s f lt/l
mkdir lt/d
touch -h -d '2020-01-02 03:04' lt/f lt/l lt/d
(cd lt && printf 'f\nl\nd\n' | "$CPIO" -o -H newc) > lt.cpio
run "$CPIO" -it -F lt.cpio
same "-t" "$(printf 'f\nl\nd')" "$out"
same "-t: status" 0 "$rc"
uid=$(id -u); gid=$(id -g)
run "$CPIO" -itvn -F lt.cpio
want=$(printf '%s %3s %-8s %-8s %8s %s %s\n' \
    -rw-r--r-- 1 "$uid" "$gid" 6 'Jan  2  2020' f \
    lrwxrwxrwx 1 "$uid" "$gid" 1 'Jan  2  2020' 'l -> f' \
    drwxr-xr-x 1 "$uid" "$gid" 0 'Jan  2  2020' d)
same "-tvn" "$want" "$out"
if [ -n "$GNU" ]; then
    run "$CPIO" -itv -F lt.cpio
    ours=$out
    "$GNU" -itv -F lt.cpio > .gnu 2>/dev/null
    same "-tv as GNU's" "$(cat .gnu)" "$ours"
fi
# listing applies no extraction safety: an absolute name is listed
printf 'src/f\n' | "$CPIO" -o -H odc | sed 's|src/f|/rc/f|' > abs.cpio
run "$CPIO" -it -F abs.cpio
same "-t of an absolute name" "/rc/f" "$out"
same "-t of an absolute name: status" 0 "$rc"
same "-t of an absolute name: quiet" "" "$err"

# ---- bin: written in this machine's (little-endian) order, read in either
printf 'src/f\n' | "$CPIO" -o -H bin > ourbin.cpio
same "bin magic, little-endian" "c771" "$(head -c 2 ourbin.cpio | od -An -tx1 | tr -d ' \n')"
# a big-endian member: one of ours with every 16-bit word swapped (the
# name "xx" and data "zz" read the same swapped), then a little-endian
# trailer
mkdir -p be && printf 'zz' > be/xx
(cd be && printf 'xx\n' | "$CPIO" -o -H bin) > be1.cpio
head -c 32 be1.cpio | dd conv=swab status=none > be.cpio
tail -c +33 be1.cpio >> be.cpio
fresh xb
run sh -c "cd xb && \"$CPIO\" -i < ../be.cpio"
same "big-endian bin" "zz" "$(cat xb/xx 2>&1)"
same "big-endian bin: status" 0 "$rc"
if [ -n "$GNU" ]; then
    printf 'src/f\n' | "$GNU" -o -H bin 2>/dev/null > gnubin.cpio
    fresh xb
    run sh -c "cd xb && \"$CPIO\" -id < ../gnubin.cpio"
    same "GNU bin, ours reading" "hello" "$(cat xb/src/f 2>&1)"
    run "$GNU" -it -F ourbin.cpio
    same "our bin, GNU reading" "src/f" "$out"
    same "our bin, GNU reading: no byte-order warning" "1 block" "$err"
fi

# ---- a file too large for the format's size field is refused, not
# truncated (a sparse 4 GiB + 1: newc and bin hold 4 GiB - 1)
truncate -s 4294967297 big
for fmt in newc bin; do
    run sh -c "printf 'big\nsrc/f\n' | \"$CPIO\" -o -H $fmt > big-$fmt.cpio"
    same "4 GiB + 1, $fmt" "cpio: big: too large for the $fmt format" "$err"
    same "4 GiB + 1, $fmt: status" 1 "$rc"
    run "$CPIO" -it -F "big-$fmt.cpio"
    same "4 GiB + 1, $fmt: left out" "src/f" "$out"
done
rm -f big

# ---- a file that yields less than its size: padded with zeros, so the
# archive stays in step.  sysfs files report 4096 bytes and read fewer.
short=
for s in /sys/kernel/*; do
    if [ -f "$s" ] && [ -r "$s" ] && [ "$(stat -c %s "$s")" -gt 0 ] &&
       [ "$(wc -c < "$s" 2>/dev/null)" -lt "$(stat -c %s "$s")" ]; then
        short=$s; break
    fi
done
if [ -n "$short" ]; then
    size=$(stat -c %s "$short"); got=$(wc -c < "$short")
    run sh -c "printf '%s\nsrc/f\n' \"$short\" | \"$CPIO\" -o > short.cpio"
    same "shrank: warning" "cpio: $short: shrank by $((size - got)) bytes; padding with zeros" "$err"
    same "shrank: status" 1 "$rc"
    run "$CPIO" -it -F short.cpio
    same "shrank: archive in step" "$(printf '%s\nsrc/f' "$short")" "$out"
else
    echo "no short-reading sysfs file: shrink case skipped"
fi

# ---- a failure to write the archive is fatal
if [ -w /dev/full ]; then
    run sh -c "printf 'src/f\n' | \"$CPIO\" -o > /dev/full"
    same "write error" "cpio: write error: No space left on device" "$err"
    same "write error: status" 2 "$rc"
fi

# ---- copy-out: newc carries a linked file's data on its last name, odc
# and bin on every name (the size column of -tv: name size, in order)
mkdir hl
printf data > hl/a; ln hl/a hl/b; ln hl/a hl/c
for fmt in newc odc bin; do
    printf 'hl/a\nhl/b\nhl/c\n' | "$CPIO" -o -H "$fmt" > "hl-$fmt.cpio"
    sizes=$("$CPIO" -itvn -F "hl-$fmt.cpio" | awk '{ printf "%s %s,", $9, $5 }')
    case $fmt in
    newc) want='hl/a 0,hl/b 0,hl/c 4,' ;;
    *)    want='hl/a 4,hl/b 4,hl/c 4,' ;;
    esac
    same "links on copy-out, $fmt" "$want" "$sizes"
done
# a linked file whose other names are not listed still gets its data
printf 'hl/b\n' | "$CPIO" -o -H newc > hl-one.cpio
same "one name of a linked file" "hl/b 4," "$("$CPIO" -itvn -F hl-one.cpio | awk '{ printf "%s %s,", $9, $5 }')"

# ---- safe extraction: no member is written through a symlink the archive
# itself made, if that symlink could lead out of the tree
mkdir -p esc/a esc/b victim
ln -s "$TMP/victim" esc/a/lnk
mkdir esc/b/lnk && printf 'pwned\n' > esc/b/lnk/pwn
{ (cd esc/a && printf 'lnk\n' | "$CPIO" -o -H newc) | head -c -124
  (cd esc/b && printf 'lnk/pwn\n' | "$CPIO" -o -H newc); } > esc.cpio
fresh xe
run sh -c "cd xe && \"$CPIO\" -id < ../esc.cpio"
ok "escape: nothing written outside" test ! -e victim/pwn
same "escape: refused" "cpio: lnk/pwn: Not a directory" "$err"
same "escape: the link is still made" "$TMP/victim" "$(readlink xe/lnk)"
# a relative link inside the tree is made at once and can be used
mkdir -p rel/a/real rel/b/in
ln -s real rel/a/in
printf 'ok\n' > rel/b/in/f
{ (cd rel/a && printf 'real\nin\n' | "$CPIO" -o -H newc) | head -c -124
  (cd rel/b && printf 'in/f\n' | "$CPIO" -o -H newc); } > rel.cpio
fresh xr
run sh -c "cd xr && \"$CPIO\" -id < ../rel.cpio"
same "relative link: written through" "ok" "$(cat xr/real/f 2>&1)"
same "relative link: status" 0 "$rc"
# --insecure trusts the archive's links as before
fresh xi
run sh -c "cd xi && \"$CPIO\" -id --insecure < ../esc.cpio"
same "--insecure follows the link" "pwned" "$(cat victim/pwn 2>&1)"
rm -f victim/pwn

# ---- a symlink member whose size is past PATH_MAX is refused, its data
# skipped, and the archive read on: an odc symlink of 8 GiB - 1 claimed
# bytes would otherwise be a huge allocation (or, on i386, a truncated
# one and a stream out of step)
ln -s f lk
printf 'lk\nsrc/f\n' | "$CPIO" -o -H odc > lk.cpio
# the odc size field of the first member (bytes 65-75): 00000010001
# makes it 4097 bytes, past PATH_MAX, which zeros after it cover
{ head -c 65 lk.cpio; printf '00000010001'; tail -c +77 lk.cpio; head -c 8000 /dev/zero; } > lkbig.cpio
fresh xl
run sh -c "cd xl && \"$CPIO\" -id < ../lkbig.cpio"
same "long symlink target" "cpio: lk: symbolic link target too long" "$(head -1 .err)"
ok "long symlink target: not made" test ! -L xl/lk

if [ "$fail" -ne 0 ]; then
    exit 1
fi
echo "ok ($cases cases)"
