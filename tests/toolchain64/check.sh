#!/bin/sh
#
# check.sh — build the tests/toolchain64 programs with the
# x86_64-unknown-substrate cross toolchain and check what it produced.
#
# This runs on the build host, so it cannot run the programs; it checks
# what can be read off them: that each links at all, is ELFCLASS64 for
# x86-64, names /sbin/ld64.so as its interpreter, takes its libraries
# from the shared lib64 set, uses 4 KiB pages, and (for the dynamic ones)
# carries the PT_GNU_EH_FRAME segment the unwinder needs.  The programs
# are left in $OUT so they can be copied to a 64-bit substrate system and
# run there; each prints "<name>: OK" and exits 0.
#
#   hello   hello.c
#   cxx     cxx.cpp
#
# Both are dynamically linked.  -static is not tested because neither
# substrate cross compiler supports it: the driver's LIB_SPEC names
# libc.so.0 outright (gcc/config/substrate.h).
#
# Env: STAGE1_PREFIX (default /opt/substrate), OUT (default ./out beside
# this script).  Exits with the number of failed checks.

set -u

: "${STAGE1_PREFIX:=/opt/substrate}"
HERE="$(cd "$(dirname "$0")" && pwd)"
: "${OUT:=$HERE/out}"

T=x86_64-unknown-substrate
CC="$STAGE1_PREFIX/bin/$T-gcc"
CXX="$STAGE1_PREFIX/bin/$T-g++"
READELF="$STAGE1_PREFIX/bin/$T-readelf"
fail=0

ok()   { echo "ok   $*"; }
bad()  { echo "FAIL $*"; fail=$((fail + 1)); }

[ -x "$CC" ] || { echo "check.sh: no $CC" >&2; exit 1; }
mkdir -p "$OUT"

# build NAME COMPILER ARGS...
build() {
    name=$1; shift
    if "$@" -o "$OUT/$name" > "$OUT/$name.log" 2>&1; then
        ok "$name links"
    else
        bad "$name does not link:"
        sed 's/^/     /' "$OUT/$name.log" | head -20
        return 1
    fi
}

# Every program: 64-bit, x86-64, 4 KiB segment alignment.
check_common() {
    f="$OUT/$1"
    h=$("$READELF" -hW "$f" 2>/dev/null)
    echo "$h" | grep -q 'Class:.*ELF64' && ok "$1 is ELFCLASS64" ||
        bad "$1 is not ELFCLASS64"
    echo "$h" | grep -q 'Machine:.*X86-64' && ok "$1 is x86-64" ||
        bad "$1 is not x86-64"
    if "$READELF" -lW "$f" | awk '$1 == "LOAD" && $NF != "0x1000" { bad = 1 }
                                   END { exit bad }'; then
        ok "$1 has 4 KiB segment alignment"
    else
        bad "$1 has a PT_LOAD not aligned to 4 KiB"
    fi
}

# check_dynamic NAME NEEDED...
check_dynamic() {
    name=$1; shift
    check_common "$name"
    f="$OUT/$name"
    if "$READELF" -lW "$f" | grep -q 'interpreter: /sbin/ld64.so\]'; then
        ok "$name interpreter is /sbin/ld64.so"
    else
        bad "$name interpreter is not /sbin/ld64.so"
    fi
    "$READELF" -lW "$f" | grep -q GNU_EH_FRAME &&
        ok "$name has PT_GNU_EH_FRAME" || bad "$name has no PT_GNU_EH_FRAME"
    needed=$("$READELF" -dW "$f" | sed -n 's/.*(NEEDED).*\[\(.*\)\]/\1/p')
    for lib in "$@"; do
        echo "$needed" | grep -qx "$lib" && ok "$name needs $lib" ||
            bad "$name does not need $lib (a static copy was linked?)"
    done
}

if build hello "$CC" -O2 -Wall "$HERE/hello.c"; then
    check_dynamic hello libc.so.0
fi
if [ -x "$CXX" ]; then
    if build cxx "$CXX" -O2 -Wall "$HERE/cxx.cpp"; then
        check_dynamic cxx libstdc++.so.6 libgcc_s.so.1 libc.so.0
    fi
else
    bad "no $CXX"
fi

# The driver must not hand the linker the build host's library directories
# (contrib/gcc/install-specs.sh): to a 64-bit link they hold compatible
# objects, and libtool names /usr/lib64 at every install-time relink.
hostdirs=$("$CC" -v -L/usr/lib64 -L /usr/lib -o "$OUT/hello" "$HERE/hello.c" 2>&1 |
           grep collect2 | tr ' ' '\n' | grep -c '^-L/usr/lib' || true)
[ "$hostdirs" = 0 ] && ok "-L/usr/lib64 and -L/usr/lib are dropped from the link" ||
    bad "the driver passes the build host's library directories to ld"

echo "toolchain64: $fail failure(s); programs are in $OUT"
exit $fail
