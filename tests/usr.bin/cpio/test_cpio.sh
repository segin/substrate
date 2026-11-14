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

if [ "$fail" -ne 0 ]; then
    exit 1
fi
echo "ok ($cases cases)"
