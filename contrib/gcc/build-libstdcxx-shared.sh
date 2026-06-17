#!/usr/bin/env bash
# build-libstdcxx-shared.sh — build libgcc_s.so.1 and libstdc++.so.6
# against the substrate stage-1 cross toolchain.
#
# This is a follow-up step to contrib/gcc/build.sh stage 1.  The
# main build.sh currently passes --disable-shared / --disable-libstdcxx
# so the stage-1 install ships only the .a flavours; this script does
# the (re)configure with --enable-shared, builds libgcc + libstdc++
# as shared, manually relinks libstdc++.so against -lc -lgcc_s
# (libtool doesn't add postdeps for the substrate target yet), and
# installs the .so + libgcc_s.so.1 into both
#   /opt/substrate/i386-unknown-substrate/lib/        (for -l: search)
#   /opt/substrate/lib/gcc/i386-unknown-substrate/16.1.0/  (for shared-libgcc)
#
# Prerequisites:
#   - contrib/gcc/build.sh stage 1 has already been run (gives us xgcc).
#   - contrib/binutils stage 1 installed (gives us as/ld/ar for the target).
#   - substrate libc.so.0 staged at $SYSROOT/usr/lib/libc.so.0.
#
# Run it from anywhere; paths are derived from $0.

set -e

HERE="$(cd -- "$(dirname -- "$0")" && pwd)"
SUBSTRATE_TOP="$(cd -- "$HERE/../.." && pwd)"
STAGE1_PREFIX="${STAGE1_PREFIX:-/opt/substrate}"
TARGET_TRIPLE="i386-unknown-substrate"
SRC_TREE="$HERE/build/gcc-16.1.0"
BUILD_DIR="$HERE/build-shared"
SYSROOT="${SYSROOT:-$SUBSTRATE_TOP/dist}"
PARALLEL="${PARALLEL:-$(nproc 2>/dev/null || echo 4)}"

export PATH="$STAGE1_PREFIX/bin:$PATH"

if [ ! -d "$SRC_TREE" ]; then
    echo "build-libstdcxx-shared.sh: source tree missing — run contrib/gcc/fetch.sh first" >&2
    exit 1
fi
if ! command -v "${TARGET_TRIPLE}-gcc" >/dev/null 2>&1; then
    echo "build-libstdcxx-shared.sh: ${TARGET_TRIPLE}-gcc not on PATH — run build.sh stage 1 first" >&2
    exit 1
fi

# Refresh sysroot headers from the in-tree include/.  GCC's
# include-fixed/ snapshots are taken at toolchain install time; if
# the libc headers gained extern "C" wrappers or new prototypes (e.g.
# nanosleep, assert __cplusplus guards), we need both the sysroot and
# the fixincludes copy refreshed before libstdc++ can be (re)built.
echo "==> Refreshing sysroot + GCC fixincludes from substrate include/"
mkdir -p "$SYSROOT/usr/include/sys"
cp -r "$SUBSTRATE_TOP/include/." "$SYSROOT/usr/include/"
FIXDIR="$STAGE1_PREFIX/lib/gcc/$TARGET_TRIPLE/16.1.0/include-fixed"
mkdir -p "$FIXDIR/sys"
cp "$SUBSTRATE_TOP/include/assert.h"        "$FIXDIR/assert.h"
cp "$SUBSTRATE_TOP/include/sys/statvfs.h"   "$FIXDIR/sys/statvfs.h"
# math.h gained C99 float_t/double_t after the toolchain's fixincludes
# snapshot was taken; libstdc++ <cmath> does `using ::double_t` and fails
# against the stale copy.  Refresh it (and the sysroot copy below).
cp "$SUBSTRATE_TOP/include/math.h"          "$FIXDIR/math.h"

# Toplevel reconfigure.  Compared to build.sh stage 1 we drop
# --disable-libstdcxx and --disable-shared so libstdc++-v3 actually
# gets built and as both .a + .so.  --disable-libstdcxx-threads /
# --disable-tls stay set because substrate's runtime isn't ready for
# the GD/LD TLS models yet.
echo "==> Configuring (--enable-shared)"
rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"
"$SRC_TREE/configure" \
    --target="$TARGET_TRIPLE" \
    --prefix="$STAGE1_PREFIX" \
    --with-sysroot="$SYSROOT" \
    --enable-languages=c,c++ \
    --enable-shared \
    --disable-multilib --disable-nls --disable-bootstrap \
    --disable-libgomp --disable-libitm --disable-libsanitizer \
    --disable-libquadmath --disable-libvtv --disable-libssp \
    --disable-libada --disable-libphobos --disable-libcc1 \
    --disable-libstdcxx-pch --disable-libstdcxx-verbose \
    --disable-libstdcxx-filesystem-ts --disable-libstdcxx-debug \
    --enable-threads=posix --enable-libstdcxx-threads \
    --disable-libatomic --disable-tls

# Build libgcc first (libstdc++ links against libgcc_s).
echo "==> Building libgcc (-j $PARALLEL)"
make -j "$PARALLEL" all-target-libgcc

echo "==> Building libstdc++-v3 (-j $PARALLEL)"
make -j "$PARALLEL" all-target-libstdc++-v3 || true   # libtool's link
                                                       # produces a so
                                                       # with no DT_NEEDED;
                                                       # we relink below

# Manual relink: libtool's link line for substrate uses -nostdlib
# with no -l flags at all, producing a libstdc++.so with no
# .dynamic section.  Re-execute the same link command with -lc and
# -lgcc_s appended so the resulting .so has proper DT_NEEDED entries.
echo "==> Relinking libstdc++.so.6 with -lc -lgcc_s"
SRCDIR="$BUILD_DIR/i386-unknown-substrate/libstdc++-v3/src"
cd "$SRCDIR"
"$BUILD_DIR/./gcc/xgcc" \
  -shared-libgcc -B"$BUILD_DIR/./gcc" -nostdinc++ \
  -B"$STAGE1_PREFIX/i386-unknown-substrate/bin/" \
  -B"$STAGE1_PREFIX/i386-unknown-substrate/lib/" \
  -fPIC -DPIC -D_GLIBCXX_SHARED -shared -nostdlib \
  .libs/compatibility.o .libs/compatibility-debug_list.o .libs/compatibility-debug_list-2.o \
  .libs/compatibility-atomic-c++0x.o .libs/compatibility-c++0x.o .libs/compatibility-chrono.o \
  .libs/compatibility-condvar.o .libs/compatibility-thread-c++0x.o \
  -Wl,--whole-archive \
    ../libsupc++/.libs/libsupc++convenience.a \
    ../src/c++98/.libs/libc++98convenience.a \
    ../src/c++11/.libs/libc++11convenience.a \
    ../src/c++17/.libs/libc++17convenience.a \
    ../src/c++20/.libs/libc++20convenience.a \
    ../src/c++23/.libs/libmodulesconvenience.a \
  -Wl,--no-whole-archive \
  "$BUILD_DIR/./gcc/crtendS.o" \
  "$SYSROOT/usr/lib/crtn.o" \
  -Wl,-O1 -Wl,-z -Wl,relro -Wl,--gc-sections \
  -Wl,--version-script=libstdc++-symbols.ver \
  -Wl,-soname -Wl,libstdc++.so.6 \
  -L"$SYSROOT/usr/lib" -Wl,--no-as-needed -l:libpthread.so.0 -l:libc.so.0 -lgcc_s \
  -o .libs/libstdc++.so.6.0.35

# Install both shared libraries into the locations GCC's spec file
# expects.  $STAGE1_PREFIX/i386-unknown-substrate/lib is where ld
# resolves -l: from; the GCC libdir is where shared-libgcc looks
# at link time.
TARGET_LIB="$STAGE1_PREFIX/$TARGET_TRIPLE/lib"
GCC_LIB="$STAGE1_PREFIX/lib/gcc/$TARGET_TRIPLE/16.1.0"

echo "==> Installing libstdc++.so.6 to $TARGET_LIB"
cp -v "$SRCDIR/.libs/libstdc++.so.6.0.35" "$TARGET_LIB/"
ln -sf libstdc++.so.6.0.35 "$TARGET_LIB/libstdc++.so.6"
ln -sf libstdc++.so.6.0.35 "$TARGET_LIB/libstdc++.so"

echo "==> Installing libgcc_s.so.1 to $TARGET_LIB and $GCC_LIB"
for d in "$TARGET_LIB" "$GCC_LIB"; do
    cp -v "$BUILD_DIR/gcc/libgcc_s.so.1" "$d/"
    ln -sf libgcc_s.so.1 "$d/libgcc_s.so"
done

# Install the C++ headers into the cross-toolchain prefix.  libstdc++-v3
# generates a target-specific bits/c++config.h with all the
# _GLIBCXX_HAVE_* defines that probed positive against substrate's
# refreshed sysroot — those defines diverge from stage 1's static-only
# c++config.h, so we want the build-shared version to be canonical on
# the build host AND on the rootfs.
#
# `make install` of libstdc++-v3 installs both libs (.a + .so + .la)
# and headers (~860 files / 16 MB under ${prefix}/${target}/include/
# c++/${gccver}/).  It re-installs libstdc++.so.6.0.35 with the same
# .libs/libstdc++.so.6.0.35 we relinked above, so the manual cp earlier
# isn't undone.
echo "==> Installing libstdc++-v3 headers + libs into $STAGE1_PREFIX"
make -C "$BUILD_DIR/$TARGET_TRIPLE/libstdc++-v3" install >/dev/null

# Stage the same .so files into the gcc-stage2 staging tree so that
# `build-rootfs.sh --toolchain` overlays them onto $DIST automatically.
# Without this, rootfs.img ships cc1/cc1plus/lto1/lto-dump that
# DT_NEEDED libstdc++.so.6 but no libstdc++.so.6 anywhere on the image.
#
# Layout mirrors what install-stripped-to-rootfs.sh injects post-hoc:
#   /lib/                                       ← ld.so search path
#   /usr/lib/                                   ← ld.so search path
#   /usr/i386-unknown-substrate/lib/            ← stage-2 driver -L
#   /usr/lib/gcc/i386-unknown-substrate/16.1.0/ ← shared-libgcc spec
STG="${GCC_STAGE2_STAGING:-/tmp/gcc-stage2-staging}"
GCCV="$(cat "$SRC_TREE/gcc/BASE-VER")"
STRIP="$STAGE1_PREFIX/bin/${TARGET_TRIPLE}-strip"

# Stage stripped copies side-by-side in $STG/_tmp first so we strip
# once and then fan out copies + symlinks.  Unstripped originals at
# /opt/substrate remain intact for build-host development/debug.
TMP_STG="$STG/_libstdcxx-stage"
rm -rf "$TMP_STG"
mkdir -p "$TMP_STG"
cp "$SRCDIR/.libs/libstdc++.so.6.0.35" "$TMP_STG/"
cp "$BUILD_DIR/gcc/libgcc_s.so.1"       "$TMP_STG/"
if [ -x "$STRIP" ]; then
    echo "==> Stripping staged libstdc++.so.6 + libgcc_s.so.1"
    "$STRIP" --strip-debug "$TMP_STG/libstdc++.so.6.0.35"
    "$STRIP" --strip-debug "$TMP_STG/libgcc_s.so.1"
fi

echo "==> Staging libstdc++.so.6 + libgcc_s.so.1 into $STG for build-rootfs.sh"
for sub in \
    "lib" \
    "usr/lib" \
    "usr/$TARGET_TRIPLE/lib" \
    "usr/lib/gcc/$TARGET_TRIPLE/$GCCV"; do
    d="$STG/$sub"
    mkdir -p "$d"
    install -m 755 "$TMP_STG/libstdc++.so.6.0.35" "$d/"
    ln -sf libstdc++.so.6.0.35 "$d/libstdc++.so.6"
    ln -sf libstdc++.so.6.0.35 "$d/libstdc++.so"
    install -m 755 "$TMP_STG/libgcc_s.so.1" "$d/"
    ln -sf libgcc_s.so.1 "$d/libgcc_s.so"
done
rm -rf "$TMP_STG"

# Stage C++ headers for the on-substrate compiler.  cc1plus's
# hardcoded default looks at:
#   /usr/lib/gcc/i386-unknown-substrate/16.1.0/../../../../include/c++/16.1.0
# = /usr/include/c++/16.1.0/ on the rootfs (sysroot=/, prefix=/usr).
# Source from /opt/substrate where `make install` just dropped them.
HDR_SRC="$STAGE1_PREFIX/$TARGET_TRIPLE/include/c++/$GCCV"
HDR_DST="$STG/usr/include/c++/$GCCV"
if [ -d "$HDR_SRC" ]; then
    echo "==> Staging C++ headers into $HDR_DST"
    rm -rf "$HDR_DST"
    mkdir -p "$HDR_DST"
    # `cp -a` preserves symlinks (the c++ header tree has none today
    # but several distros add bits/std_*.h aliases later).
    cp -a "$HDR_SRC"/. "$HDR_DST/"
else
    echo "==> WARNING: $HDR_SRC not found; C++ headers not staged" >&2
fi

cat <<EOF

==> Shared C++ runtime is installed.

    Build host: $TARGET_LIB + $GCC_LIB
    Staged for rootfs: $STG/{lib,usr/lib,usr/$TARGET_TRIPLE/lib,
                              usr/lib/gcc/$TARGET_TRIPLE/$GCCV}/

    \`build-rootfs.sh --toolchain --image\` will now overlay them
    onto rootfs.img with no separate install-stripped step needed.

    Verify the build-host side with:
      cat > /tmp/h.cc <<'CPP'
      #include <iostream>
      int main(){ std::cout << "hello from libstdc++.so.6\n"; }
      CPP
      ${TARGET_TRIPLE}-g++ -m32 -Wl,-rpath-link=$TARGET_LIB -o /tmp/h /tmp/h.cc
      ${TARGET_TRIPLE}-readelf -d /tmp/h | grep NEEDED
        # → libstdc++.so.6, libm.so.0, libc.so.0
EOF
