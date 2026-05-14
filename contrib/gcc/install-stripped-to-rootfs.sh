#!/usr/bin/env bash
# install-stripped-to-rootfs.sh — strip the substrate-target stage-2
# toolchain binaries + libstdc++.so / libgcc_s.so, then inject them
# into an EXISTING ext2 rootfs image.
#
# For fresh builds you usually don't need this script — `build-rootfs.sh
# --toolchain --image` now overlays libstdc++.so.6 + libgcc_s.so.1
# from $GCC_STAGE2_STAGING into dist/lib + dist/usr/lib + dist/usr/.../lib
# automatically (build-libstdcxx-shared.sh stages stripped copies there).
# This script is the post-hoc path: it takes a built rootfs.img and
# overwrites the toolchain bits in-place without rebuilding the image.
#
# build.sh stage 2 passes LDFLAGS="" + CC_FOR_BUILD=gcc /
# CXX_FOR_BUILD=g++ at configure time so the host binaries
# (cc1 / cc1plus / lto1 / lto-dump / xgcc / cpp) DT_NEEDED
# libstdc++.so.6 + libgcc_s.so.1 from /lib rather than statically
# embedding libstdc++.a / libgcc.a.  Combined with --strip-all,
# cc1 drops from 361 MB to 37 MB.
#
# Usage:
#   contrib/gcc/install-stripped-to-rootfs.sh /path/to/rootfs.img
#
# Env:
#   STG          stage-2 staging tree (default /tmp/gcc-stage2-staging)
#   STAGE1_PREFIX cross toolchain prefix (default /opt/substrate)

set -e

IMG="${1:-}"
if [ -z "$IMG" ] || [ ! -f "$IMG" ]; then
    echo "usage: $0 <rootfs.img>" >&2
    exit 1
fi

STG="${STG:-/tmp/gcc-stage2-staging}"
STAGE1_PREFIX="${STAGE1_PREFIX:-/opt/substrate}"
STRIP="$STAGE1_PREFIX/bin/i386-unknown-substrate-strip"
SO_SRC="$STAGE1_PREFIX/i386-unknown-substrate/lib"
GCCLIB="$STAGE1_PREFIX/lib/gcc/i386-unknown-substrate/16.1.0"

if [ ! -d "$STG/usr" ]; then
    echo "$0: stage-2 staging $STG/usr missing — run contrib/gcc/build.sh stage 2 first" >&2
    exit 1
fi
if [ ! -f "$SO_SRC/libstdc++.so.6.0.35" ]; then
    echo "$0: $SO_SRC/libstdc++.so.6.0.35 missing — run contrib/gcc/build-libstdcxx-shared.sh first" >&2
    exit 1
fi
if ! command -v debugfs >/dev/null 2>&1; then
    echo "$0: debugfs(8) not on PATH (install e2fsprogs)" >&2
    exit 1
fi

# Strip every executable in the staging tree.  --strip-all drops
# both .symtab and .debug_*; the binaries don't need either at
# runtime.
echo "==> Stripping stage-2 binaries"
find "$STG/usr/bin" "$STG/usr/libexec" -type f -executable | while read -r b; do
    # readelf -h is quick way to tell ELF from script
    "$STAGE1_PREFIX/bin/i386-unknown-substrate-readelf" -h "$b" >/dev/null 2>&1 || continue
    before=$(stat -c %s "$b")
    "$STRIP" --strip-all "$b" 2>/dev/null || continue
    after=$(stat -c %s "$b")
    printf "  %-60s %10d -> %10d\n" "${b#$STG/}" "$before" "$after"
done | sort -k4 -n -r | head -10

# Strip the runtime .so files in-place under SO_SRC as well — they
# get shipped both into the rootfs and remain at $SO_SRC for the
# cross compiler.
echo "==> Stripping libstdc++.so.6 + libgcc_s.so.1"
"$STRIP" --strip-debug "$SO_SRC/libstdc++.so.6.0.35"
"$STRIP" --strip-debug "$SO_SRC/libgcc_s.so.1"
"$STRIP" --strip-debug "$GCCLIB/libgcc_s.so.1" 2>/dev/null || true

# Inject all stage-2 staged content into the rootfs at /usr/...
# The simplest robust approach is to walk the staging tree and
# `debugfs write` each file.  We don't try to handle symlinks via
# debugfs — install them after.
debugfs_w() { debugfs -w -R "$*" "$IMG" 2>&1 | grep -vE 'file does not exist|^debugfs '; }

inject_file() {
    local src="$1" dst="$2"
    debugfs_w "rm $dst" >/dev/null 2>&1 || true
    debugfs_w "write $src $dst" >/dev/null
}
inject_symlink() {
    local link="$1" target="$2"
    debugfs_w "rm $link" >/dev/null 2>&1 || true
    debugfs_w "symlink $link $target" >/dev/null
}

echo "==> Injecting stage-2 binaries into $IMG"
( cd "$STG" && find usr -type f ) | while read -r rel; do
    inject_file "$STG/$rel" "/$rel"
done

# Runtime .so files into /lib so /sbin/ld.so finds them.
echo "==> Installing libstdc++.so.6 and libgcc_s.so.1 into /lib"
inject_file "$SO_SRC/libstdc++.so.6.0.35" "/lib/libstdc++.so.6.0.35"
inject_file "$SO_SRC/libgcc_s.so.1"       "/lib/libgcc_s.so.1"
inject_symlink "/lib/libstdc++.so.6"      "libstdc++.so.6.0.35"
inject_symlink "/lib/libstdc++.so"        "libstdc++.so.6.0.35"
inject_symlink "/lib/libgcc_s.so"         "libgcc_s.so.1"

# Same shared libs in /usr/i386-unknown-substrate/lib so the on-target
# stage-2 ld resolves `-lstdc++` / `-lgcc_s` at link time.
echo "==> Installing libstdc++ / libgcc_s into /usr/i386-unknown-substrate/lib"
TGT_LIB=/usr/i386-unknown-substrate/lib
inject_file "$SO_SRC/libstdc++.so.6.0.35" "$TGT_LIB/libstdc++.so.6.0.35"
inject_file "$SO_SRC/libgcc_s.so.1"       "$TGT_LIB/libgcc_s.so.1"
inject_symlink "$TGT_LIB/libstdc++.so.6"   "libstdc++.so.6.0.35"
inject_symlink "$TGT_LIB/libstdc++.so"     "libstdc++.so.6.0.35"
inject_symlink "$TGT_LIB/libgcc_s.so"      "libgcc_s.so.1"

# libgcc_s in the gcc-internal libdir too (shared-libgcc spec).
echo "==> Installing libgcc_s.so.1 into /usr/lib/gcc/i386-unknown-substrate/16.1.0/"
GCCDIR=/usr/lib/gcc/i386-unknown-substrate/16.1.0
inject_file "$SO_SRC/libgcc_s.so.1" "$GCCDIR/libgcc_s.so.1"
inject_symlink "$GCCDIR/libgcc_s.so" "libgcc_s.so.1"

echo
echo "==> Done.  Verify with:"
echo "    debugfs -R 'ls /lib' $IMG | grep libstdc++"
echo "    debugfs -R 'stat /usr/libexec/gcc/i386-unknown-substrate/16.1.0/cc1' $IMG"
