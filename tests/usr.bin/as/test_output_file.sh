#!/bin/sh
# The file the object is written to (AS-T-296 to 300).
#
# - It was made for its owner alone (0600), whatever the mask; it has
#   what the mask leaves of 0666, a file that was there being made anew.
# - `-o /dev/null` failed: the object was written and then looked for
#   in the null device.
# - `as -o x.s x.s` wrote the object over the source.  It is refused
#   before anything is read, and the source is as it was.
# - A link to a file that is not there yet could not be written to at
#   all; the file is made.  (A link to a file that is there is replaced
#   by the object, as that file would be.)
# - "elf_write_file failed: PATH" said nothing of why; the message has
#   the path and the system's reason.
#
# All as GNU as, which passes this test.
set -u

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../../.." && pwd)
AS=${AS:-"$top/usr.bin/as/as"}
work=$(mktemp -d)
trap 'chmod -R u+w "$work" 2> /dev/null; rm -rf "$work"' EXIT
cd "$work" || exit 1

fail=0
cases=0
check() {
    cases=$((cases + 1))
    [ "$2" = "$3" ] || { echo "FAIL $1: $2, not $3"; fail=1; }
}

printf '\tnop\n' > a.s

# Permissions.
(umask 027; "$AS" --64 -o new.o a.s)
check "a new file under umask 027" "$(stat -c %a new.o)" 640
(umask 022; "$AS" --64 -o usual.o a.s)
check "a new file under umask 022" "$(stat -c %a usual.o)" 644
(umask 0; "$AS" --64 -o open.o a.s)
check "a new file under umask 0" "$(stat -c %a open.o)" 666
(umask 077; "$AS" --64 -o shut.o a.s)
check "a new file under umask 077" "$(stat -c %a shut.o)" 600
chmod 644 shut.o
(umask 077; "$AS" --64 -o shut.o a.s)
check "a file that was there is made anew, by the mask" "$(stat -c %a shut.o)" 600

# A file that is no file.
"$AS" --64 -o /dev/null a.s 2> err
check "-o /dev/null" "$?" 0
[ -c /dev/null ] || { echo "FAIL /dev/null is no longer a device"; fail=1; }
# A pipe.  (GNU as cannot write an object to one: it seeks.)
if ! "$AS" --version 2> /dev/null | grep '^GNU assembler' | grep -qv '(Substrate)' && command -v mkfifo > /dev/null 2>&1; then
    mkfifo pipe
    cat pipe > piped.o &
    reader=$!
    timeout 20 "$AS" --64 -o pipe a.s 2> err
    check "-o a pipe" "$?" 0
    wait "$reader"
    check "and what came through it is the object" "$(head -c 4 piped.o | od -An -c | tr -d ' ')" '177ELF'
fi

# The output may not be a source.
cp a.s same.s
"$AS" --64 -o same.s same.s 2> err
check "the source for its own output" "$?" 1
check "and the source is as it was" "$(cat same.s)" "$(cat a.s)"
grep -q "same\.s.*same" err || { echo "FAIL the message: $(cat err)"; fail=1; }
ln same.s hard.s
"$AS" --64 -o hard.s same.s 2> err
check "the source by another name" "$?" 1
check "and it is as it was" "$(cat same.s)" "$(cat a.s)"
cp a.s b.s
"$AS" --64 -o b.s a.s b.s 2> err
check "the second of two sources" "$?" 1

# A link to a file that is not there yet is written through; one to a
# file that is there is replaced, and that file left alone.
ln -s real.o link.o
"$AS" --64 -o link.o a.s 2> err
check "a link to a file not yet there" "$?" 0
[ -L link.o ] || { echo "FAIL the link to nothing was replaced"; fail=1; }
check "what it points to is the object" "$(head -c 4 real.o | od -An -c | tr -d ' ')" '177ELF'
printf 'old' > real.o
"$AS" --64 -o link.o a.s 2> err
check "a link to a file that is there" "$?" 0
[ ! -L link.o ] || { echo "FAIL the link to a file was written through"; fail=1; }
check "the object has the link's name" "$(head -c 4 link.o | od -An -c | tr -d ' ')" '177ELF'
check "and the file it pointed to is as it was" "$(cat real.o)" old

# What cannot be written says where and why.
said() {
    cases=$((cases + 1))
    "$AS" --64 -o "$2" a.s 2> err
    rc=$?
    [ "$rc" != 0 ] || { echo "FAIL $1: assembled"; fail=1; }
    grep -q "$2.*$3" err || { echo "FAIL $1: $(tr '\n' '|' < err)"; fail=1; }
}
said "a directory that is not there" nowhere/x.o 'No such file or directory'
mkdir shut && chmod 555 shut
if [ "$(id -u)" != 0 ]; then
    said "a directory that may not be written in" shut/x.o 'Permission denied'
fi
if [ -c /dev/full ]; then
    said "a device with no room" /dev/full 'No space left on device'
fi
mkdir dir
said "a directory for the file" dir 'Is a directory'
check "and nothing is left in it" "$(ls dir | wc -l)" 0

[ "$fail" = 0 ] && echo "ok ($cases cases)"
exit "$fail"
