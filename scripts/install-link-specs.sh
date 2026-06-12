#!/bin/sh
#
# install-link-specs.sh — teach the substrate cross-gcc to resolve its own
# libc.so.0's DT_NEEDED chain at link time.
#
# libc.so.0 is built with --unresolved-symbols=ignore-all and DT_NEEDED
# libm.so.0 / libsys.so.0 (see lib/c/Makefile): it deliberately leaves
# feraiseexcept / syscall / setsid (and libm -> libgcc_s __divxc3) undefined,
# to be resolved at run time.  But the static linker, by default, neither
# follows those DT_NEEDED entries nor searches the sysroot for them, so a
# plain `cc main.c` (and every autoconf "C compiler works" probe, and every
# bare-Makefile contrib port) fails with a cascade of undefined references.
#
# substrate's own binaries dodge this with explicit -l: lines (Makefile.bin.inc)
# and the autotools ports via substrate_sysroot's LDFLAGS, but that leaves the
# toolchain unable to link a trivial program out of the box.  Install a GCC
# specs file that appends, to every link:
#   --copy-dt-needed-entries   follow the DT_NEEDED of input shared libs
#   -rpath-link <sysroot>/lib   ... and search the sysroot for them
# so the toolchain can link against its own libc with no per-project flags.
#
# Idempotent and standalone.  build.sh runs it after the toolchain step.

set -eu
: "${STAGE1_PREFIX:=/opt/substrate}"
SYSROOT="${STAGE1_PREFIX}/i386-unknown-substrate"

gccdir=$(ls -d "${STAGE1_PREFIX}"/lib/gcc/i386-unknown-substrate/*/ 2>/dev/null | head -1)
if [ -z "$gccdir" ]; then
    echo "install-link-specs: no cross gcc under ${STAGE1_PREFIX}; skipping" >&2
    exit 0
fi

cat > "${gccdir}specs" <<EOF
*link:
+ --copy-dt-needed-entries -rpath-link ${SYSROOT}/lib

EOF
echo "install-link-specs: installed ${gccdir}specs (--copy-dt-needed-entries + -rpath-link ${SYSROOT}/lib)"
