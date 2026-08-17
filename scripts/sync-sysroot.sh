#!/bin/sh
#
# sync-sysroot.sh — mirror built libraries + headers into the cross
# toolchain sysroot so the cross compiler's regular -lfoo / #include <foo.h>
# search finds them.
#
# Two sources are mirrored:
#   1. contrib ports        — every dist-<pkg>/usr/{lib,include} at the repo
#                             root (the staged output of contrib/<pkg>/build.sh)
#   2. substrate native libs — lib/<X>/libX.{so.0,a}, usr.lib/<X>/libX.{so.0,a},
#                             and the top-level include/ public headers
#
# Headers are mirrored both into $SYSROOT/include and into every GCC
# include-fixed snapshot (fixincludes wins over the sysroot, so a header that
# lives only in the sysroot is invisible to the compiler).
#
# Idempotent and standalone: it reconstructs the sysroot from existing dist-*
# outputs WITHOUT a rebuild, so CI / automation can run it any time the
# sysroot is fresh or stale.  build.sh also sources this file and calls the
# same functions during a build, so there is a single source of truth.
#
# Usage:
#   scripts/sync-sysroot.sh                # mirror ALL dist-* + native libs
#   scripts/sync-sysroot.sh libpng cairo   # mirror only these dist-<pkg>
#   STAGE1_PREFIX=/opt/substrate scripts/sync-sysroot.sh
#
# Env:
#   STAGE1_PREFIX   cross toolchain prefix         (default /opt/substrate)
#   SUBSTRATE_TOP   repo root                       (default: this script's ..)

: "${SUBSTRATE_TOP:=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)}"
: "${STAGE1_PREFIX:=/opt/substrate}"
SYSROOT="${STAGE1_PREFIX}/i386-unknown-substrate"

# Copy a directory tree of headers into every GCC include-fixed snapshot.
# -L dereferences symlinks: a header that is a symlink in the source tree
# (e.g. include/pthread.h -> ../lib/pthread/pthread.h) must become a REAL
# file in the sysroot, otherwise it lands as a dangling link the compiler
# cannot open ("pthread.h: No such file or directory").
_mirror_to_fixinc() {
    srcinc="$1"
    fixinc="${STAGE1_PREFIX}/lib/gcc/i386-unknown-substrate"
    [ -d "$fixinc" ] || return 0
    for ver in "$fixinc"/*/include-fixed; do
        [ -d "$ver" ] || continue
        cp -aL "$srcinc/." "$ver/" 2>/dev/null || true
    done
}

# Mirror one contrib port's dist-<pkg>/usr/{lib,include} into the sysroot.
sync_to_sysroot() {
    pkg="$1"
    distdir="${SUBSTRATE_TOP}/dist-overlay/dist-${pkg}"
    [ -d "$distdir/usr/lib" ] || [ -d "$distdir/usr/include" ] || return 0
    if [ -d "$distdir/usr/lib" ]; then
        mkdir -p "$SYSROOT/lib"
        cp -a "$distdir/usr/lib/." "$SYSROOT/lib/" 2>/dev/null || true
    fi
    if [ -d "$distdir/usr/include" ]; then
        mkdir -p "$SYSROOT/include"
        # -L: materialise symlinked headers as real files (see _mirror_to_fixinc).
        cp -aL "$distdir/usr/include/." "$SYSROOT/include/" 2>/dev/null || true
        _mirror_to_fixinc "$distdir/usr/include"
    fi
}

# Mirror substrate's own libs (lib/, usr.lib/) and public headers (include/).
sync_native_libs_to_sysroot() {
    [ -d "$SYSROOT/lib" ] || return 0
    for dir in "${SUBSTRATE_TOP}"/lib/*/ "${SUBSTRATE_TOP}"/usr.lib/*/; do
        [ -d "$dir" ] || continue
        name=$(basename "$dir")
        [ -f "$dir/lib$name.so.0" ] && cp "$dir/lib$name.so.0" "$SYSROOT/lib/" 2>/dev/null || true
        [ -f "$dir/lib$name.a"    ] && cp "$dir/lib$name.a"    "$SYSROOT/lib/" 2>/dev/null || true
    done
    if [ -d "${SUBSTRATE_TOP}/include" ]; then
        # -L: substrate's include/ has symlinked headers (pthread.h ->
        # ../lib/pthread/pthread.h); materialise them as real files.
        cp -aL "${SUBSTRATE_TOP}/include/." "$SYSROOT/include/" 2>/dev/null || true
        _mirror_to_fixinc "${SUBSTRATE_TOP}/include"
    fi
}

# Mirror every dist-<pkg> found at the repo root, then the native libs.
#
# NOTE: the staging trees actually live under dist-overlay/dist-<pkg> now, so
# this glob matches only dist-overlay/ itself and mirrors nothing.  Do NOT
# "fix" it to dist-overlay/dist-*/ without filtering: that set includes
# dist-freebsd and dist-netbsd, which are foreign-personality userlands for
# /perso/<os>, not substrate libraries.  Mirroring them into the cross sysroot
# overwrites lib/libc.so with a NetBSD symlink to libc.so.12 and drags in
# FreeBSD's libc.so.7, after which the cross compiler cannot link anything.
# Callers should name the packages they need via sync_to_sysroot instead.
sync_all_to_sysroot() {
    n=0
    for distdir in "${SUBSTRATE_TOP}"/dist-*/; do
        [ -d "$distdir" ] || continue
        pkg=$(basename "$distdir"); pkg=${pkg#dist-}
        sync_to_sysroot "$pkg"
        n=$((n + 1))
    done
    sync_native_libs_to_sysroot
    echo "sync-sysroot: mirrored $n contrib dist tree(s) + native libs into $SYSROOT"
}

# Run only when executed directly (not when sourced by build.sh, where $0 is
# build.sh).  POSIX-portable check on the invoked basename.
case "${0##*/}" in
sync-sysroot.sh)
    if [ "$#" -gt 0 ]; then
        for p in "$@"; do sync_to_sysroot "$p"; done
        echo "sync-sysroot: mirrored $* into $SYSROOT"
    else
        sync_all_to_sysroot
    fi
    ;;
esac
