#!/bin/sh
# smoke.sh - what the 64-bit image must be able to do, run on the image
# itself by smokeinit (tests/rootfs64/boot-test.sh).  Every check prints
# "ok" or "FAIL"; the exit status is the number of failures.
#
# It runs under the image's own /bin/sh, the in-tree shell built 64-bit,
# so getting this far already means /sbin/ld64.so loaded a 64-bit program
# against /lib64.
#
# Each check is one command string handed to `sh -c`.  The in-tree shell
# does not split "$@" into words, so a helper that takes the command as
# separate arguments cannot be written for it.

PATH=/bin:/sbin:/usr/bin:/usr/sbin
export PATH
D=/usr/libexec/rootfs64
fail=0

check() {
    if sh -c "$2" > /tmp/smoke.out 2>&1; then
        echo "ok   $1"
    else
        echo "FAIL $1"
        sed 's/^/     /' /tmp/smoke.out
        fail=$((fail + 1))
    fi
}

# The kernel and the userland are both 64-bit.
check "uname -m is x86_64"   '[ "$(uname -m)" = x86_64 ]'
check "uname -p is x86_64"   '[ "$(uname -p)" = x86_64 ]'

# The shell and init are 64-bit dynamic programs run by ld64.so.
# (The linker writes its listing to standard error: ldd(1), BUGS.)
check "/bin/sh runs under ld64.so"   'ldd /bin/sh 2>&1 | grep -q ld64.so'
check "libc comes from /lib64"       'ldd /bin/sh 2>&1 | grep -q /lib64/libc.so.0'
check "/sbin/init is a 64-bit program" 'ldd /sbin/init 2>&1 | grep -q ld64.so'

# The base utilities, each a 64-bit build.
check "ls"            'ls / | grep -q "^bin$"'
check "cat"           'cat /etc/passwd | grep -q "^root:"'
check "sed"           '[ "$(echo abc | sed s/b/X/)" = aXc ]'
check "wc"            '[ $(cat /etc/passwd | wc -l) -gt 3 ]'
check "tr"            'echo abc | tr a-c A-C | grep -q ABC'
check "sort"          '[ "$(printf "b\na\n" | sort | head -1)" = a ]'
check "ps"            'ps -p 1 | grep -q smokeinit'
check "mkdir/rmdir"   'mkdir /tmp/smoke.d && rmdir /tmp/smoke.d'
check "cp/cmp"        'cp /etc/passwd /tmp/smoke.pw && cmp /etc/passwd /tmp/smoke.pw'
check "command substitution in a loop" \
      'n=0; for i in 1 2 3 4 5; do n=$((n + $(echo 1))); done; [ "$n" = 5 ]'
check "pipes"         'ls /bin | sort | tail -1 | grep -q .'
check "df"            'df /'
check "/proc/meminfo" 'grep -q MemTotal /proc/meminfo'

# Programs built by the 64-bit cross toolchain: C, and C++ with the
# shared libstdc++ and an exception thrown and caught.
if [ -x $D/hello ]; then
    check "cross-compiled C program"   "$D/hello | grep -q 'hello: OK'"
else
    echo "skip cross-compiled C program (not installed)"
fi
if [ -x $D/cxx ]; then
    check "cross-compiled C++ program" "$D/cxx | grep -q 'cxx: OK'"
else
    echo "skip cross-compiled C++ program (not installed)"
fi

echo "smoke: $fail failure(s)"
exit $fail
