#!/bin/sh
# smoke.sh - what the 64-bit image must be able to do, run on the image
# itself by smokeinit (tests/rootfs64/boot-test.sh).  Every check prints
# "ok" or "FAIL"; the exit status is the number of failures.
#
# It runs under the image's own /bin/sh -- zsh where the image has the
# port, the in-tree shell built 64-bit where it does not -- so getting
# this far already means /sbin/ld64.so loaded a 64-bit program against
# /lib64.
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

# Contrib ports built for the 64-bit target (contrib/port64.sh), where the
# image has them.  Each of these is a different slice of the port set:
# zsh is the login shell and pulls in ncurses and libiconv, openssl and
# curl exercise the crypto and network libraries, bsdtar libarchive.
port_check() {
    if [ -x "$1" ]; then
        check "$2" "$3"
    else
        echo "skip $2 (not installed)"
    fi
}
port_check /usr/bin/zsh     "zsh"      '/usr/bin/zsh -c "print -l a b c" | wc -l | grep -q 3'
port_check /usr/bin/zsh     "zsh is a 64-bit program" 'ldd /usr/bin/zsh 2>&1 | grep -q ld64.so'
port_check /usr/bin/openssl "openssl"  '/usr/bin/openssl version | grep -q OpenSSL'
port_check /usr/bin/openssl "openssl sha256" \
    'echo abc | /usr/bin/openssl dgst -sha256 | grep -q edeaaff3f1774ad2888673770c6d64097e391bc362d7d6fb34982ddf0efd18cb'
port_check /usr/bin/curl    "curl"     '/usr/bin/curl --version | grep -q libcurl'
port_check /usr/bin/bsdtar  "bsdtar"   'cd /tmp && /usr/bin/bsdtar cf smoke.tar /etc/passwd 2>/dev/null; /usr/bin/bsdtar tf /tmp/smoke.tar | grep -q passwd'
port_check /usr/bin/gzip    "gzip"     'echo hello | /usr/bin/gzip | /usr/bin/gzip -d | grep -q hello'
port_check /usr/bin/make    "make"     '/usr/bin/make --version | grep -q "GNU Make"'
port_check /usr/bin/gdb     "gdb"      '/usr/bin/gdb --version | grep -q "GNU gdb"'
port_check /usr/bin/cmake   "cmake"    '/usr/bin/cmake --version | grep -q "cmake version"'
port_check /bin/mksh        "mksh"     '/bin/mksh -c "echo \$KSH_VERSION" | grep -q MIRBSD'
port_check /usr/bin/tclsh   "tclsh"    'echo "puts [expr 6*7]" | /usr/bin/tclsh | grep -q 42'
# X clients with no server to talk to: enough to show that the X libraries
# load and that the client gets as far as trying the display.
port_check /usr/bin/xterm   "xterm"    '/usr/bin/xterm -version | grep -q XTerm'
port_check /usr/bin/xauth   "xauth links libX11" 'ldd /usr/bin/xauth 2>&1 | grep -q /usr/lib64/libX11.so'

echo "smoke: $fail failure(s)"
exit $fail
