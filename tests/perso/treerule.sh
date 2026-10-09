#!/bin/sh
#
# treerule.sh - does a personality keep what it does by absolute path
# inside its own tree?
#
# NEEDS A PERSONALITY TREE mounted at /perso/NAME, which means an image
# built from distribution media that is not in this repository (tools/pcix,
# tools/venix, tools/xenix, tools/svr3 ...; the BSD and Linux trees are
# local images).  Runs on substrate, as the superuser.  Not part of
# `make -C tests`.
#
#     treerule.sh NAME SHELL DIR
#
#     NAME    the tree: pcix, venix, xenix, freebsd, netbsd, svr3 ...
#     SHELL   that system's shell, as a path inside the tree: /bin/sh
#     DIR     a directory that exists in the tree: /tmp
#
#     treerule.sh pcix /bin/sh /tmp
#
# The rule: a name under a directory the tree has belongs to the tree,
# whether the file exists yet or not.  The system's own shell and tools
# are made to create a file, make and remove directories, rename, link,
# change mode, unlink and change directory, all by absolute path under
# DIR, and the script then looks from outside to see where each landed.
# Every line it prints says "ok"; anything in substrate's own DIR, or
# missing from the tree, is a "FAIL".  Exits non-zero if any did.
#
# On Version 7, System III and Xenix/286 this is also the test of mkdir(1)
# and rmdir(1), which have no system call and make a directory out of
# mknod(2), link(2) and unlink(2).

if [ $# -ne 3 ]; then
    echo "usage: treerule.sh NAME SHELL DIR" >&2
    exit 2
fi
os=$1 sh=$2 d=$3
tree=/perso/$os
if [ ! -x "$tree$sh" ] || [ ! -d "$tree$d" ]; then
    echo "treerule.sh: $tree$sh or $tree$d is not there" >&2
    exit 2
fi

fails=0
ok()   { echo "  ok    $1"; }
fail() { echo "  FAIL  $1"; fails=$((fails + 1)); }
expect() {      # expect WORD DESCRIPTION: WORD is among what the shell said
    case " $said " in *" $1 "*) ok "$2" ;; *) fail "$2" ;; esac
}

for f in tr1 tr2 tr3 trd tre; do
    rm -rf "$d/$f" "$tree$d/$f" 2>/dev/null
done

said=$("$tree$sh" -c "
    echo a > $d/tr1
    ls $d/tr1 >/dev/null 2>&1 && echo created
    mkdir $d/trd && echo mkdir
    mkdir $d/tre; rmdir $d/tre && echo rmdir
    mv $d/tr1 $d/tr2 && echo rename
    ln $d/tr2 $d/tr3 && echo link
    chmod 600 $d/tr2 && echo chmod
    rm $d/tr3 && echo unlink
    ls $d/trd/. >/dev/null 2>&1 && echo dot
    cd $d/trd && echo chdir
" 2>&1 | tr '\n' ' ')

expect created "a file made by absolute path is seen by the program"
expect mkdir   "mkdir"
expect rmdir   "rmdir"
expect rename  "rename"
expect link    "link"
expect chmod   "chmod"
expect unlink  "unlink"
expect dot     "the new directory has its . entry"
expect chdir   "chdir"

[ -f "$tree$d/tr2" ] && ok "the file is in the tree" || fail "the file is in the tree"
[ -d "$tree$d/trd" ] && ok "the directory is in the tree" || fail "the directory is in the tree"
for f in tr1 tr2 tr3 trd tre; do
    [ -e "$d/$f" ] && fail "$d/$f is in substrate's own $d"
done
[ -e "$tree$d/tr1" ] || [ -e "$tree$d/tr3" ] || [ -e "$tree$d/tre" ] &&
    fail "something that was renamed or removed is still there"

rm -rf "$tree$d/tr2" "$tree$d/trd"
if [ $fails -eq 0 ]; then echo "treerule $os: PASS"; else echo "treerule $os: FAIL"; fi
exit $fails
