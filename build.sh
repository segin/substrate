#!/bin/sh
#
# build.sh — end-to-end "whole shebang" build from a clean checkout.
#
# Order (each layer depends on the previous):
#   1.  contrib/build-toolchain.sh --stage=1 — cross binutils+GCC
#       targeting i386-unknown-substrate.  Stage 2 (the Canadian
#       cross that runs ON substrate) comes later, at 1g: it has to
#       link test programs with substrate's own libc, which does not
#       exist until the native libs below are built and mirrored.
#   2.  Native substrate components (kernel `sys/`, runtime libs
#       under `lib/`, the dynamic linker `sbin/ld.so/`, base
#       userland `bin/` + `sbin/`, helper libs under `usr.lib/`,
#       toolchain-helper utilities `usr.bin/`, manuals).
#   3.  contrib/ ports — each contrib/<pkg> with a fetch.sh +
#       build.sh.  Built in dependency order.  Output goes into
#       per-package ${SUBSTRATE_TOP}/dist-overlay/dist-<pkg>/ staging trees so
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
#   SKIP_GRUB=1       don't build contrib/grub; let build-rootfs.sh fall
#                     back to whatever GRUB the build host has
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
: "${SKIP_GRUB:=0}"
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
# The X SERVER group (libXdmcp .. xorg-server) builds Xfbdev, the kdrive
# framebuffer server.  contrib/xorg-server/build.sh names its ten staged
# prerequisites explicitly; six of them -- libXdmcp, pixman, libxshmfence,
# libfontenc, libXfont, libxkbfile -- were not in this list before, and each
# needs only xorgproto / xtrans / libX11, all built above.  libXfont is
# configured --disable-freetype, so it does NOT drag in freetype + libpng.
#
# xkbcomp + xkeyboard-config and the encodings / font-util / font-* ports are
# runtime data rather than build dependencies of the server, but a server with
# no keymap compiler and no fonts is not a usable one: without ISO8859-1 fonts
# every Xmb client renders tofu.  The font ports and xkeyboard-config are pure
# staging -- they unpack and generate a fonts.dir, with no compile step and no
# host tools -- so they are cheap to carry.
#
# The CDE group is libXScrnSaver + motif + cde.  contrib/cde/build.sh merges
# twenty dist-<pkg> trees into one sysroot and refuses to start if any are
# missing; eighteen were already here, and libXScrnSaver and motif are the
# two that were not.  Both need only the X toolkit chain built above.  cde
# also wants mksh (the target's /bin/ksh), which is already in the list.
#
DEFAULT_CONTRIB="bzip2 libiconv zlib openssl ncurses gzip tzdata make sed expr libarchive mpg123 curl nginx inetutils zsh e2fsprogs e2tools gmp mpfr gdb xorgproto xcb-proto libXau xtrans libxcb libX11 libXext libICE libSM libXt libXmu libXpm libXaw libXinerama libjpeg lmdb mksh tcl libtirpc xterm xauth luit xrdb libXdmcp pixman libxshmfence libfontenc libXfont libxkbfile xkbcomp xkeyboard-config encodings font-util font-misc-misc font-adobe-75dpi font-adobe-100dpi font-bh-lucida xorg-server libXScrnSaver libXrender motif cde"
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
    note "binutils 2.46.0 + GCC 16.1.0 -- STAGE 1 ONLY (cross, runs on Linux)"
    note "installs into $STAGE1_PREFIX; stage 2 comes after the native libs exist"

    # Seed the sysroot headers FIRST.  gcc stage 1 is configured
    # --with-sysroot="$HERE/dist" and its fixincludes pass reads
    # $sysroot/usr/include while gcc is being BUILT, not after:
    #
    #   The directory (BUILD_SYSTEM_HEADER_DIR) that should contain system
    #   headers does not exist:
    #     .../dist/usr/include
    #   make: *** [Makefile:4776: all-gcc] Error 2
    #
    # dist/ is not staged until stage 3, so on a genuinely clean checkout --
    # the case this script's header advertises -- there is nothing there yet
    # and gcc cannot build.  It only ever worked on a tree that already had a
    # dist/ from an earlier run.  Stage 3 wipes and repopulates dist/ properly;
    # this just puts the headers where gcc needs them, at the point it needs
    # them.  -L because include/ has symlinked headers (pthread.h ->
    # ../lib/pthread/pthread.h) that must land as real files.
    note "seeding dist/usr/include for gcc's sysroot"
    mkdir -p dist/usr/include dist/usr/lib
    cp -aL include/. dist/usr/include/ 2>/dev/null || true

    contrib/build-toolchain.sh --stage=1
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

# Teach the cross-gcc to resolve libc.so.0's DT_NEEDED chain (libsys/libm/
# libgcc_s) at link time, so a bare `cc main.c` — and every autoconf compiler
# probe and bare-Makefile contrib port — links against substrate's own libc
# without per-project flags.  Runs here, after the sysroot has the libs the
# spec's -rpath-link points at.
#
# contrib/gcc/install-specs.sh is the one that writes this file; the specs it
# installs also carry --eh-frame-hdr (without which every C++ throw from a
# main executable reaches std::terminate) and -lpthread (without which every
# C++ link fails on pthread_mutex_lock out of libstdc++'s eh_alloc.o), and it
# asserts the libfoo.so linker names besides.
"${HERE}/contrib/gcc/install-specs.sh"

#-----------------------------------------------------------------------
# Stage 1f2: finish the stage-1 target runtime (libgcc + libstdc++).
#
# Neither can be built during stage 0: both link against substrate's libc,
# and substrate's libc is compiled BY the stage-1 cross compiler.  So stage 0
# gets a compiler with no runtime, and the failures land far away --
# "ld: cannot find -lc" for libgcc_s.so.1, and later, when stage 2's
# Canadian cross builds its own host-side C++ with the target g++,
# "C++ compiler cannot create executables" out of libcody.  Now that 1a-1e
# have built libc and 1f has mirrored it, finish the job.  Reuses the stage-1
# build tree rather than reconfiguring, which would be a whole second gcc
# build.
#-----------------------------------------------------------------------
if [ "$SKIP_TOOLCHAIN" = 1 ]; then
    step "Stage 1f2: target runtime (skipped — SKIP_TOOLCHAIN=1)"
else
    step "Stage 1f2: finish libgcc + libstdc++ now that libc is in the sysroot"
    contrib/gcc/build.sh --target-runtime

    # Again, now that libstdc++ exists: the first run could only warn about
    # it.  This is what creates the libstdc++.so LINKER name, and without it
    # -lstdc++ falls through to libstdc++.a with no diagnostic at all, giving
    # every shared object its own operator new/delete, iostream and locale
    # globals and typeinfo.  Stage 2 links C++ against this sysroot.
    "${HERE}/contrib/gcc/install-specs.sh"
fi

#-----------------------------------------------------------------------
# Stage 1f3: can the cross compiler link a program at all?
#
# Everything from here on is autoconf, and autoconf's answer to a broken
# toolchain is "C compiler cannot create executables" plus a config.log that
# lives inside the build tree.  Ask the question directly instead, while the
# answer is still a compiler diagnostic naming the missing piece.
#-----------------------------------------------------------------------
step "Stage 1f3: cross-toolchain smoke test"
_cc="${STAGE1_PREFIX:-/opt/substrate}/bin/i386-unknown-substrate-gcc"
if [ -x "$_cc" ]; then
    _t=$(mktemp -d)
    printf 'int main(void){return 0;}\n' > "$_t/t.c"
    if "$_cc" -o "$_t/t" "$_t/t.c" 2>"$_t/err"; then
        echo "    ok: $_cc links a hello-world"
    else
        echo "    FAILED: $_cc cannot link a trivial program." >&2
        cat "$_t/err" >&2
        _sysroot="${STAGE1_PREFIX:-/opt/substrate}/i386-unknown-substrate"
        echo "    --- $_sysroot/lib ---" >&2
        ls -l "$_sysroot/lib" >&2 || true
        echo "    --- libc.so.0 DT_NEEDED ---" >&2
        readelf -d "$_sysroot/lib/libc.so.0" 2>/dev/null | grep NEEDED >&2 || true
        echo "    --- libraries ld actually opened ---" >&2
        "$_cc" -Wl,--trace -o "$_t/t" "$_t/t.c" 2>&1 | head -30 >&2 || true
        echo "    --- link line ---" >&2
        "$_cc" -v -o "$_t/t" "$_t/t.c" 2>&1 | tail -20 >&2 || true
        rm -rf "$_t"
        exit 1
    fi
    rm -rf "$_t"
else
    echo "    no cross gcc at $_cc — skipping"
fi

#-----------------------------------------------------------------------
# Stage 1g: toolchain stage 2 -- AFTER the native libs, not with stage 1.
#
# Stage 2 is a Canadian cross: the binutils and gcc it produces are
# substrate ELFs, so configure has to link a test program with the cross
# compiler, which needs substrate's libc and crt0 in the sysroot.  Those do
# not exist until stage 1a-1e have built them and 1f has mirrored them, so
# running both toolchain stages together at stage 0 -- as this script used
# to -- fails every time on a tree without a prebuilt sysroot:
#
#   configure: error: C compiler cannot create executables
#
#-----------------------------------------------------------------------
if [ "$SKIP_TOOLCHAIN" = 1 ]; then
    step "Stage 1g: TOOLCHAIN STAGE 2 (skipped — SKIP_TOOLCHAIN=1)"
else
    step "Stage 1g: toolchain stage 2 (Canadian cross, runs ON substrate)"
    note "stages into dist-overlay/dist-toolchain for --toolchain to overlay"
    contrib/build-toolchain.sh --stage=2
fi

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
# Stage 2b: GRUB.
#
# A HOST-tool port, unlike everything in DEFAULT_CONTRIB: this GRUB is not
# substrate userland, it is the bootloader that loads the kernel plus the
# utilities that assemble it into rootfs.img, and it is built with the host
# compiler.  That is why it is a stage of its own rather than another entry
# in the contrib list.
#
# build-rootfs.sh's grub_setup() prefers contrib/grub/dist-grub over the
# host's /usr/lib/grub whenever the port has been built, so building it here
# is what makes the image self-contained instead of depending on whichever
# GRUB the build machine happens to have installed -- or on it having one.
# It builds three platforms: i386-pc, x86_64-efi, i386-efi.
#-----------------------------------------------------------------------
if [ "$SKIP_GRUB" = 1 ]; then
    step "Stage 2b: GRUB (skipped — SKIP_GRUB=1)"
    note "build-rootfs.sh will fall back to the host's GRUB."
else
    step "Stage 2b: GRUB for the image (host tool, not substrate userland)"
    ( cd contrib/grub && ./fetch.sh && ./build.sh )
    # Assert rather than let a silent fallback to the host's GRUB hide a
    # failure here: the bake would still succeed, with a different
    # bootloader than the one this stage exists to produce.
    _gm="${HERE}/contrib/grub/dist-grub/usr/bin/grub-mkimage"
    [ -x "$_gm" ] || {
        echo "build.sh: contrib/grub finished but $_gm is missing" >&2
        exit 1
    }
    note "GRUB $("$_gm" --version 2>/dev/null | awk '{print $NF}') staged for the image bake"
fi

#-----------------------------------------------------------------------
# Stage 3: image
#-----------------------------------------------------------------------
# --no-boot: sys/boot (the in-tree stage2 bootloader) does not compile --
# stage2.c trips -Werror=unused-function on four statics -- and without the
# flag build-rootfs.sh stops there and stages nothing.  The image boots via
# GRUB, so nothing here needs it.  .github/workflows/ci.yml's image job
# passes the same flag for the same reason.
step "Stage 3: dist/ staging"
./build-rootfs.sh --dist --no-boot
./build-rootfs.sh --toolchain

if [ "$SKIP_IMAGE" = 1 ]; then
    note "Skipping image bake (SKIP_IMAGE=1).  dist/ is ready under $HERE/dist/."
else
    step "Stage 3b: rootfs.img"
    ./build-rootfs.sh --image --no-boot
    note "Image at $HERE/rootfs.img"
fi

step "DONE"
note "Run: ./run.sh   to boot rootfs.img under qemu."
