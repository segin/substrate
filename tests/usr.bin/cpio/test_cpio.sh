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
# failures are reported on stdout; the stderr of commands not run through
# run() (block counts, mostly) is kept out of the way
exec 2>"$TMP/.stderr"
export TZ=UTC LC_ALL=C
umask 022

GNU=
if command -v cpio >/dev/null 2>&1 && cpio --version 2>/dev/null | grep -q GNU; then
    GNU=$(command -v cpio)
fi

fail=0
cases=0

# run CMD...: run it in a subshell, keeping stdout, stderr and status.
# The "N blocks" report is dropped from stderr; the block cases check it.
run() {
    "$@" >"$TMP/.out" 2>"$TMP/.err"
    rc=$?
    grep -Ev '^[0-9]+ blocks?$' "$TMP/.err" > "$TMP/.err2"
    mv "$TMP/.err2" "$TMP/.err"
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

# notrailer NEWC-ARCHIVE: its members, without the trailer and padding
notrailer() {
    head -c "$(( $(grep -abo 'TRAILER!!!' "$1" | head -1 | cut -d: -f1) - 110 ))" "$1"
}

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
    same "our bin, GNU reading: no byte-order warning" "" "$err"
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
(cd esc/a && printf 'lnk\n' | "$CPIO" -o -H newc --quiet) > esc1.cpio
{ notrailer esc1.cpio
  (cd esc/b && printf 'lnk/pwn\n' | "$CPIO" -o -H newc --quiet); } > esc.cpio
fresh xe
run sh -c "cd xe && \"$CPIO\" -id < ../esc.cpio"
ok "escape: nothing written outside" test ! -e victim/pwn
same "escape: refused" "cpio: lnk/pwn: Not a directory" "$err"
same "escape: the link is still made" "$TMP/victim" "$(readlink xe/lnk)"
# a relative link inside the tree is made at once and can be used
mkdir -p rel/a/real rel/b/in
ln -s real rel/a/in
printf 'ok\n' > rel/b/in/f
(cd rel/a && printf 'real\nin\n' | "$CPIO" -o -H newc --quiet) > rel1.cpio
{ notrailer rel1.cpio
  (cd rel/b && printf 'in/f\n' | "$CPIO" -o -H newc --quiet); } > rel.cpio
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

# ---- pass mode replaces a symlink at the destination, not writes through it
mkdir -p pd outside
ln -s ../outside/target pd/f
run sh -c "printf 'f\n' | (cd src && \"$CPIO\" -pu ../pd)"
ok "-p: nothing written through the link" test ! -e outside/target
same "-p: copy in place of the link" "hello" "$(cat pd/f 2>&1)"
ok "-p: no link left" test ! -L pd/f
# ... nor deletes the source when it is its own destination
run sh -c "printf 'f\n' | (cd src && \"$CPIO\" -pu .)"
same "-p onto itself: refused" "cpio: f: is the same file as its copy" "$err"
same "-p onto itself: source kept" "hello" "$(cat src/f 2>&1)"

# ---- modes are the archive's, not cut down by the umask (022 here)
mkdir md md/sticky
printf 'm\n' > md/sg; chmod 2775 md/sg
chmod 1777 md/sticky
mkfifo -m 0620 md/fifo
(cd md && printf 'sg\nsticky\nfifo\n' | "$CPIO" -o) > md.cpio
fresh xm
run sh -c "cd xm && \"$CPIO\" -id < ../md.cpio"
same "mode: setgid file" 2775 "$(stat -c %a xm/sg)"
same "mode: sticky directory" 1777 "$(stat -c %a xm/sticky)"
same "mode: fifo" 620 "$(stat -c %a xm/fifo)"
same "mode: status" 0 "$rc"
printf 'g\n' > md/gw; chmod 664 md/gw
fresh pm
(cd md && printf 'gw\n' | "$CPIO" -p ../pm)
same "-p mode" 664 "$(stat -c %a pm/gw)"

# ---- a file on disk no older than the member stays, unless -u
fresh xu
printf 'NEWER\n' > xu/f; touch -d '+1 day' xu/f
printf 'f\n' | (cd src && "$CPIO" -o) > u.cpio
run sh -c "cd xu && \"$CPIO\" -i < ../u.cpio"
same "newer kept" "NEWER" "$(cat xu/f)"
same "newer kept: message" "cpio: f not created: newer or same age version exists" "$err"
same "newer kept: status" 0 "$rc"
run sh -c "cd xu && \"$CPIO\" -iu < ../u.cpio"
same "-u replaces" "hello" "$(cat xu/f)"
fresh pu
printf 'NEWER\n' > pu/f; touch -d '+1 day' pu/f
run sh -c "printf 'f\n' | (cd src && \"$CPIO\" -p ../pu)"
same "-p: newer kept" "NEWER" "$(cat pu/f)"
same "-p: newer kept: message" "cpio: ../pu/f not created: newer or same age version exists" "$err"
# an older file is replaced without -u
touch -d '2001-01-01' pu/f
run sh -c "printf 'f\n' | (cd src && \"$CPIO\" -p ../pu)"
same "-p: older replaced" "hello" "$(cat pu/f)"

# ---- a read-only directory still takes its contents; -m keeps a
# directory's time though files are made in it after it
mkdir -p rod/ro
printf 'z\n' > rod/ro/z
touch -d '2001-02-03 04:05' rod/ro
chmod 555 rod/ro
(cd rod && printf 'ro\nro/z\n' | "$CPIO" -o) > ro.cpio
fresh xro
run sh -c "cd xro && \"$CPIO\" -idm < ../ro.cpio"
same "read-only directory: contents" "z" "$(cat xro/ro/z 2>&1)"
same "read-only directory: status" 0 "$rc"
same "read-only directory: mode" 555 "$(stat -c %a xro/ro)"
same "-m on a directory" "2001-02-03 04:05" "$(date -u -d "@$(stat -c %Y xro/ro)" '+%Y-%m-%d %H:%M')"
fresh pro
run sh -c "cd rod && printf 'ro\nro/z\n' | \"$CPIO\" -pdm ../pro"
same "-p read-only directory: contents" "z" "$(cat pro/ro/z 2>&1)"
same "-p read-only directory: mode" 555 "$(stat -c %a pro/ro)"
same "-p -m on a directory" "2001-02-03 04:05" "$(date -u -d "@$(stat -c %Y pro/ro)" '+%Y-%m-%d %H:%M')"
chmod 755 rod/ro xro/ro pro/ro

# ---- -R: USER:GROUP, USER, :GROUP, USER: (login group), names or numbers;
# on copy-out it sets the header, on copy-in the files' owner
owners() { "$CPIO" -itvn -F "$1" | awk '{ printf "%s %s", $3, $4 }'; }
for spec in 123:456 123 :456 "$(id -un):" "$(id -un).$(id -gn)"; do
    run sh -c "printf 'src/f\n' | \"$CPIO\" -o -R '$spec' > own.cpio"
    case $spec in
    123:456) want="123 456" ;;
    123)     want="123 $gid" ;;
    :456)    want="$uid 456" ;;
    *)       want="$uid $gid" ;;
    esac
    same "-R $spec" "$want" "$(owners own.cpio)"
done
run sh -c "printf 'src/f\n' | \"$CPIO\" -o -R nosuchuser0:"
same "-R unknown user" "cpio: invalid user: nosuchuser0" "$err"
same "-R unknown user: status" 2 "$rc"
printf 'src/f\n' | "$CPIO" -o -R 123:456 > own.cpio
fresh xo
run sh -c "cd xo && \"$CPIO\" -id -R $uid:$gid < ../own.cpio"
same "-i -R to oneself" "$uid $gid" "$(stat -c '%u %g' xo/src/f)"
same "-i -R to oneself: status" 0 "$rc"
if [ "$uid" -ne 0 ]; then
    fresh xo
    run sh -c "cd xo && \"$CPIO\" -id -R 0:0 < ../own.cpio"
    same "-i -R 0:0, not root" "cpio: src/f: Operation not permitted" "$err"
    same "-i -R 0:0, not root: status" 1 "$rc"
fi

# ---- pass mode: fifos copied, hard links kept, -l links, nothing made
# for a source that cannot be read, -v names only what was copied
mkdir -p ps
mkfifo -m 0640 ps/fifo
printf 'L\n' > ps/l1; ln ps/l1 ps/l2
printf 'secret\n' > ps/noread; chmod 000 ps/noread
fresh pp
run sh -c "cd ps && printf 'fifo\nl1\nl2\nnoread\n' | \"$CPIO\" -pv ../pp"
ok "-p fifo" test -p pp/fifo
same "-p fifo mode" 640 "$(stat -c %a pp/fifo 2>&1)"
same "-p links kept" "$(stat -c %i pp/l1)" "$(stat -c %i pp/l2)"
if [ "$uid" -ne 0 ]; then
    ok "-p unreadable source: no empty copy" test ! -e pp/noread
    same "-p unreadable source" "cpio: noread: Permission denied" "$(grep noread .err)"
    same "-p -v names what was copied" "$(printf 'fifo\nl1\nl2')" "$(grep -v '^cpio:' .err)"
    same "-p unreadable source: status" 1 "$rc"
fi
chmod 600 ps/noread
fresh pl
run sh -c "cd ps && printf 'l1\n' | \"$CPIO\" -pl ../pl"
same "-pl links to the source" "$(stat -c %i ps/l1)" "$(stat -c %i pl/l1)"

# ---- copy-in patterns: operands, -E file, -f to invert; * matches /
printf 'src/f\nsrc/d\nsrc/d/g\n' | "$CPIO" -o > pat.cpio
same "pattern" "src/d/g" "$("$CPIO" -it -F pat.cpio 'src/d/*')"
same "patterns" "$(printf 'src/f\nsrc/d/g')" "$("$CPIO" -it -F pat.cpio 'src/f' '*/g')"
same "-f" "$(printf 'src/f\nsrc/d')" "$("$CPIO" -itf -F pat.cpio 'src/d/*')"
printf 'src/f\n\nsrc/d\n' > pats
same "-E" "$(printf 'src/f\nsrc/d')" "$("$CPIO" -it -E pats -F pat.cpio)"
fresh xpat
run sh -c "cd xpat && \"$CPIO\" -id 'src/d/*' < ../pat.cpio"
same "pattern extracts only matches" "xpat/src/d/g" "$(find xpat -type f)"

# ---- options: GNU's long names, "--", an argument that looks like an
# option, and the combinations that make no sense
printf 'src/f\n' | "$CPIO" --create --format=odc --file=long.cpio
same "long options" "src/f" "$("$CPIO" --extract --list --file long.cpio)"
cp long.cpio ./--insecure
same "-F takes an argument like an option" "src/f" "$("$CPIO" -it -F --insecure)"
same "-- ends the options" "src/f" "$("$CPIO" -it -F long.cpio -- 'src/*')"
same "-t alone lists" "src/f" "$("$CPIO" -t -F long.cpio)"
run "$CPIO" -o -i
same "two modes" "cpio: only one of -o, -i and -p may be given" "$err"
same "two modes: status" 2 "$rc"
run "$CPIO" -o stray </dev/null
same "copy-out operand" "cpio: too many arguments: stray (copy-out reads its names from standard input)" "$err"
run "$CPIO" -p -F x.cpio dir </dev/null
same "-p with -F" "cpio: -F names an archive: -p has none" "$err"
run "$CPIO" --frobnicate
same "unknown long option" "cpio: unknown option --frobnicate" "$(head -1 .err)"
run "$CPIO" -v </dev/null
same "no mode" "cpio: one of -o, -i and -p is needed" "$(head -1 .err)"

# ---- -c is -H newc; -I and -O name the archive; -D changes directory
printf 'src/f\n' | "$CPIO" -oc -O io.cpio
same "-c writes newc" "070701" "$(head -c 6 io.cpio)"
same "-I" "src/f" "$("$CPIO" -it -I io.cpio)"
run "$CPIO" -o -I io.cpio </dev/null
same "-I with -o" "cpio: -I names the archive to read: it goes with -i" "$err"
fresh xD
run "$CPIO" -id -D xD -I "$TMP/io.cpio"
same "-D" "hello" "$(cat xD/src/f 2>&1)"
run "$CPIO" -i -D nonexistentdir -I io.cpio
same "-D to nowhere" "cpio: nonexistentdir: No such file or directory" "$err"

# ---- -0 reads a NUL-separated list; a name too long for a path is
# reported whole, not cut into two names
mkdir -p nl && printf 'n\n' > "nl/a
b"
printf 'nl/a\nb\0src/f\0' | "$CPIO" -o0 > nul.cpio
same "-0" "$(printf 'nl/a\nb\nsrc/f')" "$("$CPIO" -it -F nul.cpio)"
fresh p0
printf 'nl/a\nb\0' | "$CPIO" -p0d p0
same "-p -0" "n" "$(cat "p0/nl/a
b" 2>&1)"
long=$(head -c 5000 /dev/zero | tr '\0' x)
run sh -c "printf '%s\nsrc/f\n' $long | \"$CPIO\" -o > longname.cpio"
same "long name" "cpio: $(printf %.40s "$long")...: name too long" "$err"
same "long name: the rest archived" "src/f" "$("$CPIO" -it -F longname.cpio)"

# ---- -L archives what a symlink points to; -a puts access times back
ln -s f src/lnk
same "-L" "-rw-r--r-- 6 src/lnk" "$(printf 'src/lnk\n' | "$CPIO" -oL | "$CPIO" -itv | awk '{ print $1, $5, $9 }')"
fresh pL
printf 'src/lnk\n' | "$CPIO" -pdL pL
ok "-p -L copies the file" test -f pL/src/lnk -a ! -L pL/src/lnk
rm src/lnk
printf 'atime\n' > at; touch -a -d '2001-01-01 00:00' at
printf 'at\n' | "$CPIO" -oa > /dev/null
same "-a" "2001-01-01" "$(date -u -d "@$(stat -c %X at)" +%F)"

# ---- the archive is written in whole blocks, and their number reported
# on stderr as GNU cpio's: 512 bytes, -B 5120, -C N, --block-size=N * 512
head -c 3000 /dev/urandom > blk
for o in "" -B "-C 1000" --block-size=3; do
    run sh -c "printf 'src/f\nblk\n' | \"$CPIO\" -o $o 2>&1 >blk.cpio"
    size=$(stat -c %s blk.cpio)
    case $o in
    "")  want="7 blocks 3584" ;;
    -B)  want="1 block 5120" ;;
    -C*) want="4 blocks 4000" ;;
    *)   want="3 blocks 4608" ;;
    esac
    same "blocks, copy-out ${o:-512}" "$want" "$out $size"
    same "blocks, copy-in ${o:-512}" "${want% *}" "$("$CPIO" -it $o -F blk.cpio 2>&1 >/dev/null)"
    if [ -n "$GNU" ]; then
        same "blocks, ${o:-512}: GNU reads it" "$(printf 'src/f\nblk')" "$("$GNU" -it $o -F blk.cpio 2>/dev/null)"
    fi
done
fresh pb
same "blocks, pass mode" "6 blocks" "$(printf 'src/f\nblk\n' | "$CPIO" -pd pb 2>&1)"
same "--quiet" "" "$(printf 'src/f\n' | "$CPIO" -o --quiet 2>&1 >/dev/null)"

# ---- -V: a dot a file, then a newline, before the block count
same "-V" "$(printf '..\n1 block')" "$(printf 'src/f\nsrc/d/g\n' | "$CPIO" -oV 2>&1 >/dev/null)"
fresh xV
same "-iV" "$(printf '...\n1 block')" "$(cd xV && "$CPIO" -idV < ../pat.cpio 2>&1)"

# ---- -r asks on the terminal for each name; an empty reply skips the
# member (script(1) supplies the terminal)
if command -v script >/dev/null 2>&1; then
    fresh xr2
    (cd xr2 && { sleep 1; printf 'renamed\n'; sleep 1; printf '\n'; sleep 1; } |
        script -qec "\"$CPIO\" -ird -I ../pat.cpio 'src/f' 'src/d/g'" /dev/null >/dev/null 2>&1)
    same "-r renames" "hello" "$(cat xr2/renamed 2>&1)"
    ok "-r: an empty reply skips" test ! -e xr2/src/d/g
else
    echo "no script(1): -r case skipped"
fi
run "$CPIO" -o -r </dev/null
same "-r with -o" "cpio: -r renames members as they are extracted: it goes with -i" "$err"

if [ "$fail" -ne 0 ]; then
    exit 1
fi
echo "ok ($cases cases)"
