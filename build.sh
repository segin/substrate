#!/bin/sh
#
# build.sh — end-to-end "whole shebang" build from a clean checkout.
#
# Order (each layer depends on the previous):
#   1.  contrib/build-toolchain.sh — stage-1 cross binutils+GCC
#       targeting i386-unknown-substrate, then a Canadian-cross
#       stage 2 that runs ON substrate.
#   2.  Native substrate components (kernel `sys/`, runtime libs
#       under `lib/`, the dynamic linker `sbin/ld.so/`, base
#       userland `bin/` + `sbin/`, helper libs under `usr.lib/`,
#       toolchain-helper utilities `usr.bin/`, manuals).
#   3.  contrib/ ports — each contrib/<pkg> with a fetch.sh +
#       build.sh.  Built in dependency order.  Output goes into
#       per-package ${SUBSTRATE_TOP}/dist-<pkg>/ staging trees so
#       partial rebuilds don't perturb each other.
#   4.  build-rootfs.sh — assembles dist/ from `sys/`, the libs,
#       every dist-*/ overlay, and bakes rootfs.img.
#
# Env knobs (all optional):
#
#   STAGE1_PREFIX     where the cross toolchain installs           default /opt/substrate
#   JOBS              -j parallelism                                default `nproc`
#   SKIP_TOOLCHAIN=1  reuse an existing $STAGE1_PREFIX, don't rebuild
#   SKIP_CONTRIB=1    skip every contrib/ port (just kernel + userland + image)
#   SKIP_IMAGE=1      stop after staging dist/, don't bake rootfs.img
#   ONLY="pkg1 pkg2"  build only these contrib ports (default: everything in
#                      the order baked into this script)
#
# A clean run from a fresh checkout (no root required):
#
#   git clone … substrate && cd substrate
#   ./build.sh
#
# Building images NEVER needs root: build-rootfs.sh bakes rootfs.img with
# mke2fs + debugfs entirely in userspace (no loopback mount; setuid and
# owner=root bits are set INSIDE the image via debugfs `sif`, not on the
# host).  The only step that writes outside the source tree is the stage-1
# toolchain install into $STAGE1_PREFIX (default /opt/substrate).  If that
# directory is not writable by you, make it yours ONCE
# (`sudo install -d -o "$(id -un)" /opt/substrate`) or point STAGE1_PREFIX
# at a user-owned path — then every build, images included, runs without
# sudo.  SKIP_TOOLCHAIN=1 skips that step when the toolchain is already
# installed.
#
# Output:
#   ./rootfs.img   bootable ext2 image
#   ./dist/        staged root filesystem

set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"
cd "$HERE"

: "${STAGE1_PREFIX:=/opt/substrate}"
: "${JOBS:=$(nproc 2>/dev/null || echo 4)}"
: "${SKIP_TOOLCHAIN:=0}"
: "${SKIP_CONTRIB:=0}"
: "${SKIP_IMAGE:=0}"
export STAGE1_PREFIX JOBS SUBSTRATE_TOP="$HERE"

# contrib build order.  Each name is a directory under contrib/.
# Dependencies (build BEFORE consumers — install must reach the
# cross sysroot before downstream configure can find -lfoo):
#
#   bzip2          → libarchive (--with-bz2lib)
#   libiconv       → curl, mpg123, zsh (DT_NEEDED libiconv.so.2)
#   openssl        → curl (--with-openssl)
#   ncurses        → zsh, inetutils, less, vi — anything that calls
#                    tigetstr/setupterm.  Supersedes the lib/curses
#                    link-time stub at runtime.
#   make/tzdata/sed/expr/gzip/libarchive/mpg123/curl → independent
#                    of each other; ordered after the libraries they
#                    might link against.
#   zsh            → DT_NEEDED libiconv, libncurses → must come AFTER
#                    libiconv + ncurses.
#   inetutils      → uses libncurses for some clients (telnet, etc.)
#   xorgproto      → libXau, libxcb, libX11 (X protocol headers).
#   xcb-proto      → libxcb (build-time xcbgen Python generator).
#   libXau         → libxcb (DT_NEEDED libXau.so.6).
#   xtrans         → libX11 (transport .c files, header-only).
#   libxcb         → libX11 (DT_NEEDED libxcb.so.1) — needs
#                    xorgproto + xcb-proto + libXau first.
#   libX11         → Xlib — needs xorgproto + xtrans + libxcb first.
#   e2fsprogs      → e2tools (libext2fs + libcom_err); independent
#                    of every other contrib port.
#   libXext/libICE → the X toolkit chain.  Build order is
#   libSM/libXt/     dependency-forced: libICE before libSM before
#   libXmu/libXpm/   libXt; libXext before libXmu/libXpm; libXt +
#   libXaw           libXmu + libXext + libXpm before libXaw.
#   xterm          → terminal emulator — needs the whole X toolkit
#                    chain + ncurses (already built above).
#   xauth          → X authority tool — needs libX11/libXau/libXext/
#                    libXmu; lets X clients authenticate to a server.
#   luit           → Unicode/locale ISO-2022 filter — needs libfontenc
#                    + libiconv (no Xlib); xterm starts it via -lc to
#                    bridge a UTF-8 locale to a legacy-encoded child.
#   zlib           → nginx (--with-http_gzip_static_module), also a
#                    general DEFLATE runtime.  Build before nginx.
#   nginx          → HTTP/HTTPS server — needs zlib + openssl staged
#                    (build.sh points -I/-L at dist-zlib + dist-openssl).
#                    Cross-built via nginx's --crossbuild mechanism.
#
DEFAULT_CONTRIB="bzip2 libiconv zlib openssl ncurses gzip tzdata make sed expr libarchive mpg123 curl nginx inetutils zsh e2fsprogs e2tools xorgproto xcb-proto libXau xtrans libxcb libX11 libXext libICE libSM libXt libXmu libXpm libXaw libXinerama libjpeg lmdb mksh tcl libtirpc xterm xauth luit xrdb"
: "${ONLY:=${DEFAULT_CONTRIB}}"

#
# Mirror built libs + headers into the cross-toolchain sysroot so each
# contrib's configure finds the previously-built ones with the regular
# -lfoo / #include search.  Without this, configure probes for
# libiconv/libpng/libssl/... silently disable features we just built.
#
# The logic lives in scripts/sync-sysroot.sh so it is ALSO runnable
# standalone to reconstruct the sysroot from existing dist-* outputs
# without a rebuild (automation: scripts/sync-sysroot.sh).  Sourcing it
# defines sync_to_sysroot() and sync_native_libs_to_sysroot() — the same
# names the build loop below calls — so there is one source of truth.
#
. "${HERE}/scripts/sync-sysroot.sh"

step() { printf '\n=========================  %s  =========================\n' "$*"; }
note() { printf '   %s\n' "$*"; }

#-----------------------------------------------------------------------
# Stage 0: toolchain
#-----------------------------------------------------------------------
if [ "$SKIP_TOOLCHAIN" = 1 ]; then
    step "Stage 0: TOOLCHAIN (skipped — SKIP_TOOLCHAIN=1)"
    note "Reusing the existing cross toolchain at $STAGE1_PREFIX."
else
    step "Stage 0: TOOLCHAIN (contrib/build-toolchain.sh)"
    note "binutils 2.46.0 + GCC 16.1.0, stage 1 cross + stage 2 Canadian cross"
    note "installs into $STAGE1_PREFIX (stage 1) and /tmp/gcc-stage2-staging (stage 2)"
    contrib/build-toolchain.sh
fi

export PATH="${STAGE1_PREFIX}/bin:${PATH}"

#-----------------------------------------------------------------------
# Stage 1: native substrate components
#-----------------------------------------------------------------------
step "Stage 1a: kernel (sys/)"
make -C sys -j"$JOBS"

step "Stage 1b: substrate runtime libraries (lib/, usr.lib/)"
make -C lib
make -C usr.lib

step "Stage 1c: dynamic linker (sbin/ld.so/)"
make -C sbin/ld.so

step "Stage 1d: base userland (bin/, sbin/)"
make -C bin
make -C sbin

step "Stage 1e: toolchain-helper utilities (usr.bin/)"
make -C usr.bin

step "Stage 1f: sync native libs + headers into cross sysroot"
sync_native_libs_to_sysroot

#-----------------------------------------------------------------------
# Stage 2: contrib ports
#-----------------------------------------------------------------------
if [ "$SKIP_CONTRIB" = 1 ]; then
    step "Stage 2: CONTRIB (skipped — SKIP_CONTRIB=1)"
else
    for pkg in $ONLY; do
        [ -d "contrib/$pkg" ] || { echo "build.sh: no such contrib/$pkg" >&2; exit 1; }
        [ -x "contrib/$pkg/fetch.sh" ] || { echo "build.sh: contrib/$pkg has no fetch.sh" >&2; exit 1; }
        [ -x "contrib/$pkg/build.sh" ] || { echo "build.sh: contrib/$pkg has no build.sh" >&2; exit 1; }

        step "Stage 2: contrib/$pkg"
        ( cd "contrib/$pkg" && ./fetch.sh )
        ( cd "contrib/$pkg" && ./build.sh )
        sync_to_sysroot "$pkg"
    done
fi

#-----------------------------------------------------------------------
# Stage 3: image
#-----------------------------------------------------------------------
step "Stage 3: dist/ staging"
./build-rootfs.sh --dist
./build-rootfs.sh --toolchain

if [ "$SKIP_IMAGE" = 1 ]; then
    note "Skipping image bake (SKIP_IMAGE=1).  dist/ is ready under $HERE/dist/."
else
    step "Stage 3b: rootfs.img"
    ./build-rootfs.sh --image
    note "Image at $HERE/rootfs.img"
fi

step "DONE"
note "Run: ./run.sh   to boot rootfs.img under qemu."
