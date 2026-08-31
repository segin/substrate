#!/bin/bash
# Build script to create root filesystem and images

set -e

TOP="$(cd "$(dirname "$0")" && pwd)"
DIST="$TOP/dist"
IMAGE="${IMAGE:-$TOP/rootfs.img}"
# Overridable so a smaller image can be baked for a quick boot test:
#   IMAGE_SIZE_MIB=512 ./build-rootfs.sh --image
IMAGE_SIZE_MIB="${IMAGE_SIZE_MIB:-4096}"
BOOT_DIR="$TOP/sys/boot"

usage() {
    echo "Usage: $0 [options]"
    echo ""
    echo "Options:"
    echo "  --dist       Prepare the dist/ directory with all distribution files"
    echo "  --toolchain  Overlay contrib stage-2 toolchain staging trees onto dist/usr/"
    echo "               (gcc + binutils built by contrib/build-toolchain.sh --stage=2)"
    echo "  --image      Create a ${IMAGE_SIZE_MIB}MiB ext2 filesystem image (rootfs.img)"
    echo "  --no-boot    Skip building the sys/boot bootloader"
    echo "  --help       Show this help message"
    echo ""
    echo "Typical full bootstrap sequence:"
    echo "  contrib/build-toolchain.sh           # binutils + gcc stage 1 (cross) and stage 2 (substrate)"
    echo "  $0 --dist                            # substrate userland into dist/"
    echo "  $0 --toolchain                       # overlay gcc + binutils on top"
    echo "  $0 --image                           # bake rootfs.img"
    echo ""
    exit 1
}

# Stage-2 toolchain staging trees are produced by contrib/build-toolchain.sh
# under SUBSTRATE_TOP/dist-toolchain (binutils) and /tmp/gcc-stage2-staging
# (gcc) — see contrib/{binutils,gcc}/build.sh for the canonical paths.
DIST_TOOLCHAIN="${DIST_TOOLCHAIN:-$TOP/dist-overlay/dist-toolchain}"
GCC_STAGE2_STAGING="${GCC_STAGE2_STAGING:-/tmp/gcc-stage2-staging}"

overlay_toolchain() {
    local merged=0
    for staging in "$DIST_TOOLCHAIN" "$GCC_STAGE2_STAGING"; do
        if [ ! -d "$staging" ]; then
            echo "  (skipped: $staging does not exist)"
            continue
        fi
        # Overlay both the usr/ subtree (binaries + headers + libs)
        # and the optional top-level lib/ subtree (libstdc++.so.6 +
        # libgcc_s.so.1 staged by build-libstdcxx-shared.sh land
        # here so the runtime ends up at /lib on rootfs.img).
        for sub in usr lib; do
            [ -d "$staging/$sub" ] || continue
            mkdir -p "$DIST/$sub"
            echo "Overlaying $staging/$sub/ -> $DIST/$sub/"
            # --remove-destination is critical: a plain `cp -a` would
            # WRITE THROUGH existing symlinks (substrate's homebrew
            # install creates `ranlib -> ar` and friends in dist/usr/bin/).
            # Without --remove-destination, copying the new `ranlib`
            # binary onto that symlink overwrites `ar` instead — and
            # both names then resolve to the same ranlib content.
            # --remove-destination unlinks the destination first so the
            # new file replaces the symlink cleanly.
            cp -a --remove-destination "$staging/$sub"/. "$DIST/$sub/" 2>/dev/null || {
                # Fallback for non-GNU cp: explicit unlink + copy.
                (cd "$staging/$sub" && find . \( -type f -o -type l \)) | while read -r f; do
                    rm -f "$DIST/$sub/${f#./}"
                done
                cp -a "$staging/$sub"/. "$DIST/$sub/"
            }
            merged=$((merged + 1))
        done
    done
    if [ "$merged" = 0 ]; then
        echo "Error: no stage-2 toolchain staging found." >&2
        echo "       run contrib/build-toolchain.sh --stage=2 first." >&2
        exit 1
    fi
    echo "Toolchain overlay complete."
}

# Build the mandoc.db keyword database over dist/usr/share/man.
#
# man(1), apropos(1) and whatis(1) on substrate are mandoc (contrib/mandoc,
# 1.14.6), which resolves pages through a per-tree mandoc.db produced by
# makewhatis(8).  Without one, every lookup reports
#
#     outdated mandoc.db lacks <page>(<sec>) entry, run makewhatis /usr/share/man
#
# and apropos/whatis find nothing at all.  The database cannot be generated
# on the target: the mandoc port stages apropos/whatis/man/mandoc/soelim/
# demandoc but not makewhatis, so there is nothing on the image to build it
# with.  It is a build artifact regardless -- the man tree is fixed once the
# image is baked -- so generate it here.
#
# The host's makewhatis writes the same on-disk format as long as it is the
# same mandoc release as the port.  Rather than parse a version (mandoc has
# no --version), verify the result functionally: a format mismatch does not
# fail makewhatis, it produces a database the reader silently ignores, so a
# lookup that finds nothing is the signal to warn about.
build_man_db() {
    local mandir="$DIST/usr/share/man"

    [ -d "$mandir" ] || return 0

    if ! command -v makewhatis >/dev/null 2>&1; then
        echo "  (skipped mandoc.db: no makewhatis(8) on the build host --" \
             "man -k / apropos will find nothing on the target)" >&2
        return 0
    fi

    echo "Building mandoc.db under dist/usr/share/man..."
    if ! makewhatis "$mandir" 2>&1 | sed 's/^/  /'; then
        echo "  WARNING: makewhatis failed; target ships without a man database" >&2
        return 0
    fi
    if [ ! -s "$mandir/mandoc.db" ]; then
        echo "  WARNING: makewhatis produced no mandoc.db" >&2
        return 0
    fi

    # Functional check with the host's own reader, which is the same
    # implementation the target runs.
    if command -v apropos >/dev/null 2>&1 &&
       ! apropos -M "$mandir" mandoc >/dev/null 2>&1; then
        echo "  WARNING: mandoc.db built but the host apropos cannot read it;" \
             "host and target mandoc versions may differ" >&2
        return 0
    fi
    echo "  mandoc.db: $(du -h "$mandir/mandoc.db" | cut -f1)," \
         "$(find "$mandir" -type f -name '*.[1-9]' | wc -l) pages indexed"
}

clean_dist() {
    echo "Cleaning dist directory..."
    rm -rf "$DIST"
    mkdir -p "$DIST"/{bin,sbin,usr/{bin,lib,include},lib,dev,etc,proc,tmp,home,root,boot,sys}
    # /var subtree.  Standard hier(7) layout — most subdirs are
    # initially empty but services expect them to exist or they
    # silently fail their own mkdir-after-EACCES and produce no logs.
    mkdir -p "$DIST/var"/{empty,log,run,spool,spool/mail,tmp,cache,lib,lock}
    # /var/run/utmp + /var/log/wtmp are touched by init/login.  Pre-create
    # empty files with sensible perms so first-write doesn't have to
    # decide on a mode.
    : > "$DIST/var/run/utmp"
    : > "$DIST/var/log/wtmp"
    : > "$DIST/var/log/lastlog"
    chmod 0644 "$DIST/var/run/utmp" "$DIST/var/log/wtmp" "$DIST/var/log/lastlog"
    chmod 1777 "$DIST/var/tmp"
    chmod 1777 "$DIST/tmp"
}

build_bootloader() {
    echo "Building Substrate bootloader..."
    make -C "$BOOT_DIR"
}

build_components() {
    echo "Building kernel..."
    make -C "$TOP/sys" -j4

    # libc.so.0 links -l:libsys.so.0 and -l:libm.so.0, so both have to exist
    # before libc is built.  Neither depends on libc in turn, so there is no
    # cycle -- but with libc first, a tree that has never built them (a fresh
    # clone, a new worktree, CI) died at "cannot find -l:libsys.so.0".  Same
    # trap as lib/at below.
    echo "Building runtime libraries libc depends on..."
    make -C "$TOP/lib/sys" -j4
    make -C "$TOP/lib/m" -j4

    echo "Building libc..."
    make -C "$TOP/lib/c" -j4

    echo "Building runtime libraries..."
    make -C "$TOP/lib/pthread" -j4
    make -C "$TOP/lib/dl" -j4
    make -C "$TOP/lib/edit" -j4
    # usr.bin/at links -lat, so libat has to exist before the usr.bin pass.
    # Without this a tree that has never built lib/at (a fresh clone or a new
    # worktree) dies at "cannot find -lat" instead of producing a dist/.
    make -C "$TOP/lib/at" -j4

    echo "Building usr.lib helper libraries (libelfobj, libregex,"
    echo "libexvi, libbc, libdemangle, libjoin, libuu, ...)"
    # Drive the dispatch through usr.lib/Makefile so its SUBDIRS order and
    # inter-library prereqs (libexvi.so.0 needs libregex.so.0) are honored.
    # Iterating "usr.lib/*/" in glob order builds them alphabetically, so
    # exvi runs before regex and fails to link.
    make -C "$TOP/usr.lib" -j4

    echo "Building dynamic linker..."
    make -C "$TOP/sbin/ld.so" -j4

    echo "Building target toolchain..."
    for dir in cc as ld ar ranlib nm objdump objcopy readelf strip strings size addr2line elfedit; do
        if [ -f "$TOP/usr.bin/$dir/Makefile" ]; then
            make -C "$TOP/usr.bin/$dir" -j4
        fi
    done

    echo "Building all usr.bin programs (ldd, sysctl, ...)"
    for dir in "$TOP/usr.bin"/*/ ; do
        if [ -f "$dir/Makefile" ]; then
            make -C "$dir" -j4
        fi
    done

    echo "Building userland..."
    make -C "$TOP/bin" -j4

    echo "Building sbin (init, ld.so, ...)"
    for dir in "$TOP/sbin"/*/ ; do
        # sdm (the display manager / sgreet Xlib client) needs the contrib X
        # stack and is cross-built + installed separately below; this generic
        # loop passes no CROSS, so sdm's Makefile (CC=$(CROSS)gcc) would fall
        # back to the host gcc and fail "-march=i486 without -m32".  Skip it.
        [ "$(basename "$dir")" = sdm ] && continue
        if [ -f "$dir/Makefile" ]; then
            make -C "$dir" -j4
        fi
    done

    echo "Building usr.sbin..."
    for dir in "$TOP/usr.sbin"/*/ ; do
        if [ -f "$dir/Makefile" ]; then
            make -C "$dir" -j4
        fi
    done

    echo "Building target testsuites (tests/lib/sockets, tests/lib/ipc, tests/lib/pty, tests/lib/signal, ...)..."
    # The cross-toolchain's sysroot ships an older libc.so.0 from
    # when stage 1 was installed.  Tests that link against newer
    # libc additions (mkfifo, pipe2, the socket syscall wrappers,
    # ...) fail at link time until the sysroot is refreshed.  Do it
    # in-line here so every build sees the freshly-built libc.
    if [ -f "$TOP/lib/c/libc.so.0" ] && \
       [ -d /opt/substrate/i386-unknown-substrate/lib ]; then
        cp "$TOP/lib/c/libc.so.0" \
           /opt/substrate/i386-unknown-substrate/lib/libc.so.0
    fi

    # Cross-build the portable POSIX testsuites so the binaries are
    # ready for install_to_dist() to drop into /tmp on the image.
    # Each Makefile follows the CROSS=PREFIX convention; the listed
    # subdirs each produce one or more standalone test programs.
    for dir in "$TOP/tests/lib/sockets" "$TOP/tests/lib/ipc" "$TOP/tests/lib/pty" "$TOP/tests/lib/signal" "$TOP/tests/lib/futex"; do
        if [ -f "$dir/Makefile" ]; then
            make -C "$dir" clean >/dev/null
            # Tolerant: a broken test build (e.g. cross-libc out of sync)
            # shouldn't stop the rest of the rootfs from going together.
            # The image will simply be missing that test binary.
            make -C "$dir" \
                CROSS=/opt/substrate/bin/i386-unknown-substrate- -j4 \
                || echo "build-rootfs: warning: failed to build $dir, skipping"
        fi
    done
    # tests/lib/c is a grab-bag of libc tests with per-target makefiles
    # (Makefile.fcntl, ...) rather than a single default Makefile.
    make -C "$TOP/tests/lib/c" -f Makefile.fcntl clean >/dev/null 2>&1 || true
    make -C "$TOP/tests/lib/c" -f Makefile.fcntl \
        CROSS=/opt/substrate/bin/i386-unknown-substrate- -j4 \
        || echo "build-rootfs: warning: failed to build tests/lib/c torture_fcntl, skipping"
}

finalize_x_fonts() {
    # Adobe + B&H Lucida fonts both stage into X11/100dpi and 75dpi, each
    # with its own fonts.dir — regenerate ONE merged fonts.dir per dir from
    # every BDF present, and install the CDE -dt-* font aliases.  dtcm (and
    # other CDE apps) build FontSets from -dt-* / -b&h-lucida*; a font that
    # the set can't resolve makes Xfbdev SEGFAULT (it dies holding the evdev
    # grab -> frozen desktop), so all the referenced fonts + aliases must
    # be present.
    _cde_alias="$TOP/contrib/cde/build/cdesktopenv/cde/programs/fontaliases"
    # Only 100dpi + 75dpi gained fonts (adobe + B&H share them); regenerate
    # one merged fonts.dir there.  Leave misc alone — font-misc-misc owns it
    # (its fonts.alias gives xterm "fixed"/"9x15").
    for _d in 100dpi 75dpi; do
        _dst="$DIST/usr/share/fonts/X11/$_d"
        [ -d "$_dst" ] || continue
        : > "$_dst/fonts.dir.tmp"; _n=0
        for _f in "$_dst"/*.bdf; do
            [ -f "$_f" ] || continue
            _bn=$(basename "$_f")
            _xlfd=$(sed -n 's/^FONT[[:space:]][[:space:]]*\(.*\)$/\1/p' "$_f" | head -1)
            [ -n "$_xlfd" ] || continue
            printf '%s\t%s\n' "$_bn" "$_xlfd" >> "$_dst/fonts.dir.tmp"; _n=$((_n+1))
        done
        { printf '%d\n' "$_n"; cat "$_dst/fonts.dir.tmp"; } > "$_dst/fonts.dir"
        cp -a "$_dst/fonts.dir" "$_dst/fonts.scale"; rm -f "$_dst/fonts.dir.tmp"
        echo "Finalized X fonts in $_d: $_n entries"
    done
    # CDE -dt-* aliases in ONE dir (100dpi); X merges fonts.alias across the
    # whole path so they resolve globally, and misc keeps its own aliases.
    _dst="$DIST/usr/share/fonts/X11/100dpi"
    if [ -f "$_cde_alias/mixed.alias" ] && [ -d "$_dst" ]; then
        cat "$_cde_alias/mixed.alias" "$_cde_alias/fixed.alias" > "$_dst/fonts.alias" 2>/dev/null || \
            cp "$_cde_alias/mixed.alias" "$_dst/fonts.alias"
        echo "Installed CDE -dt-* font aliases in 100dpi"
    fi
}

install_to_dist() {
    echo "Installing kernel to dist/boot and dist/vmunix..."
    cp "$TOP/sys/kernel.bin" "$DIST/boot/"
    cp "$TOP/sys/kernel.multiboot" "$DIST/boot/"
    [ -f "$TOP/sys/kernel.fb.bin" ] && cp "$TOP/sys/kernel.fb.bin" "$DIST/boot/"
    # /vmunix is what every bootloader loads: GRUB (BIOS and EFI) and the
    # ext2 stage2.  Use the FRAMEBUFFER variant -- its multiboot header asks
    # for a linear framebuffer (mode_type=0) rather than EGA text, and UEFI
    # has no EGA text mode to give, so the text variant cannot be booted from
    # an ESP at all.  It is also what reaches the graphical login on BIOS.
    if [ -f "$TOP/sys/kernel.fb.bin" ]; then
        cp "$TOP/sys/kernel.fb.bin" "$DIST/vmunix"
    else
        cp "$TOP/sys/kernel.multiboot" "$DIST/vmunix"
    fi

    echo "Installing libc + runtime libraries to dist/usr/lib + dist/lib..."
    mkdir -p "$DIST/usr/include"
    cp -r "$TOP/include/"* "$DIST/usr/include/"

    # Every PIE binary substrate ships DT_NEEDED at minimum libc.so.0 +
    # libsys.so.0; getty / login also pull libpthread + libm; gcc on
    # substrate pulls libstdc++ + libgcc_s (overlaid from gcc staging).
    #
    # libc.so.0 is installed to /lib ONLY, and deliberately.  ld.so searches
    # its built-in trusted directories /lib, /usr/lib and /usr/local/lib in
    # that order, so one copy in /lib already resolves for everyone -- the
    # second copy bought nothing and cost correctness.  Shipping two libcs
    # means two files that can drift, and they did: an image was found
    # carrying a current /lib/libc.so.0 beside a twelve-day-old
    # /usr/lib/libc.so.0.  Only search order kept the stale one from being
    # loaded, and anything resolving out of /usr/lib first would have got a
    # libc that did not match the ld.so it was running under.
    #
    # lib/c/Makefile's own install target has always done it this way (libc.a
    # to /usr/lib, libc.so.0 to /lib); this loop was the odd one out.
    for libdir in c sys m pthread dl edit resolv; do
        if [ -f "$TOP/lib/$libdir/lib$libdir.a" ]; then
            cp "$TOP/lib/$libdir/lib$libdir.a" "$DIST/usr/lib/"
        fi
        if [ -f "$TOP/lib/$libdir/lib$libdir.so.0" ]; then
            cp "$TOP/lib/$libdir/lib$libdir.so.0" "$DIST/lib/"
            ln -sf "lib$libdir.so.0" "$DIST/lib/lib$libdir.so"
            if [ "$libdir" != c ]; then
                cp "$TOP/lib/$libdir/lib$libdir.so.0" "$DIST/usr/lib/"
                ln -sf "lib$libdir.so.0" "$DIST/usr/lib/lib$libdir.so"
            fi
        fi
    done
    # Sweep any libc left in /usr/lib by an earlier build of this script.
    rm -f "$DIST/usr/lib/libc.so.0" "$DIST/usr/lib/libc.so"

    # libgcc_s.so.1 is the unwind/divide/multiply runtime libm.so.0
    # DT_NEEDEDs.  Without it, ld.so fatal-errors on EVERY dynamic
    # binary load because libm is in libc.so.0's needed list (libc
    # uses softfloat helpers from libgcc on i386).  The toolchain
    # owns this file: install it from the cross sysroot, where
    # build-libstdcxx-shared.sh stripped it down to ~185 KB.  Fall
    # back to the heavier debug copy under the gcc internal lib dir
    # if the stripped one isn't present.  Without this, a fresh
    # `--dist` ALWAYS produces an unbootable image even if a prior
    # `--toolchain` overlay had once landed the file — `--dist`
    # wipes dist/lib.
    : "${STAGE1_PREFIX:=/opt/substrate}"
    _libgcc_src=""
    for cand in \
        "$STAGE1_PREFIX/i386-unknown-substrate/lib/libgcc_s.so.1" \
        "$STAGE1_PREFIX/lib/gcc/i386-unknown-substrate/16.1.0/libgcc_s.so.1"; do
        if [ -f "$cand" ]; then _libgcc_src="$cand"; break; fi
    done
    if [ -n "$_libgcc_src" ]; then
        cp "$_libgcc_src" "$DIST/lib/libgcc_s.so.1"
        cp "$_libgcc_src" "$DIST/usr/lib/libgcc_s.so.1"
        ln -sf libgcc_s.so.1 "$DIST/lib/libgcc_s.so"
        ln -sf libgcc_s.so.1 "$DIST/usr/lib/libgcc_s.so"
    else
        echo "build-rootfs: WARNING libgcc_s.so.1 not found under $STAGE1_PREFIX" >&2
        echo "build-rootfs:   the image will boot-panic in ld.so" >&2
    fi
    unset _libgcc_src

    # libstdc++.so.6 is the shared C++ runtime that CDE's dtsession (and the
    # rest of the C++ desktop) DT_NEEDED.  Like libgcc_s.so.1 it is owned by
    # the toolchain and lives in the cross sysroot (built by
    # contrib/gcc/build-libstdcxx-shared.sh); stage it from there so a fresh
    # --dist — which wipes dist/lib — ALWAYS lands it, not only when a
    # --toolchain overlay happens to carry it.  Without it ld.so fatal-errors
    # ("libstdc++.so.6 — not found") and the CDE login loops back to sgreet.
    _libstdcxx_src=$(ls "$STAGE1_PREFIX/i386-unknown-substrate/lib"/libstdc++.so.6.[0-9]* 2>/dev/null \
                     | grep -v -- '-gdb.py' | head -1)
    if [ -n "$_libstdcxx_src" ] && [ -f "$_libstdcxx_src" ]; then
        _soname=$(basename "$_libstdcxx_src")
        cp "$_libstdcxx_src" "$DIST/lib/$_soname"
        cp "$_libstdcxx_src" "$DIST/usr/lib/$_soname"
        ln -sf "$_soname" "$DIST/lib/libstdc++.so.6"
        ln -sf "$_soname" "$DIST/usr/lib/libstdc++.so.6"
    else
        echo "build-rootfs: WARNING libstdc++.so.6 not found under $STAGE1_PREFIX" >&2
        echo "build-rootfs:   CDE (dtsession) and other C++ binaries will fail to load" >&2
    fi
    unset _libstdcxx_src _soname

    # crt0.o lives next to libc.a — userland Makefiles reference it as
    # $(TOP)/lib/c/crt0.o at link time, but on-target it's expected at
    # /usr/lib/crt0.o for the substrate-native compiler.
    # crti.o / crtn.o pair: GCC's SysV-style startfile spec wraps user
    # code with `crti.o ... crtbegin.o ... <user> ... crtend.o ... crtn.o`.
    # The substrate-native g++ driver expects them at /usr/lib/ —
    # without them `g++ foo.cpp` fails with "cannot find crti.o".
    for crt in crt0.o crti.o crtn.o; do
        if [ -f "$TOP/lib/c/$crt" ]; then
            cp "$TOP/lib/c/$crt" "$DIST/usr/lib/"
        fi
    done

    # The native dynamic linker every PIE binary names as PT_INTERP.
    if [ -f "$TOP/sbin/ld.so/ld.so" ]; then
        cp "$TOP/sbin/ld.so/ld.so" "$DIST/sbin/ld.so"
        chmod +x "$DIST/sbin/ld.so"
    fi

    echo "Installing userland binaries to dist/bin..."
    for dir in "$TOP/bin"/*/ ; do
        if [ -d "$dir" ]; then
            name=$(basename "$dir")
            # In-tree bin/tar/tar is superseded by contrib/libarchive's
            # bsdtar (installed at /usr/bin/tar via the overlay).  Skip
            # so we don't shadow the working binary with the broken one.
            [ "$name" = "tar" ] && continue
            if [ -f "$dir/$name" ]; then
                cp "$dir/$name" "$DIST/bin/"
            fi
        fi
    done

    # grep is a single binary that also serves as egrep/fgrep (selected by
    # argv[0]).  The copy loop above stages only the `grep` binary, so run
    # grep's install target to drop the egrep/fgrep POSIX-name wrappers.
    # They are shebang scripts, not symlinks, because debugfs populates the
    # image from regular files but not relative symlinks (a symlinked egrep
    # silently never landed on-target — "command not found: egrep").
    if [ -f "$DIST/bin/grep" ]; then
        make -C "$TOP/bin/grep" install-grep-links DESTDIR="$DIST" >/dev/null 2>&1 || true
    fi

    # inetutils-telnetd has /usr/bin/login compiled in as the exec
    # target; substrate ships login at /bin/login.  Symlink so the
    # default path resolves without needing a custom -E flag in
    # inetd.conf.
    mkdir -p "$DIST/usr/bin"
    if [ ! -e "$DIST/usr/bin/login" ]; then
        ln -sf ../../bin/login "$DIST/usr/bin/login"
    fi
    # CDE's dtsession_res (dtloadresources) hardcodes /usr/bin/tr; substrate
    # ships tr at /bin/tr.  Without this the CDE session start logs
    # "/usr/bin/tr: inaccessible or not found" and the X resource load fails.
    if [ ! -e "$DIST/usr/bin/tr" ]; then
        ln -sf ../../bin/tr "$DIST/usr/bin/tr"
    fi

    echo "Installing substrate-native man pages from usr.man/..."
    make -C "$TOP/usr.man" install DESTDIR="$DIST" >/dev/null

    echo "Installing configuration from etc/..."
    cp -r "$TOP/etc/." "$DIST/etc/"

    # Mirror the precompiled terminfo database to its canonical
    # location.  /etc/terminfo is also a path ncurses-style consumers
    # check, but every shell/editor convention expects
    # /usr/share/terminfo/<first-char>/<name>, so install there too.
    if [ -d "$TOP/etc/terminfo" ]; then
        mkdir -p "$DIST/usr/share/terminfo"
        cp -r "$TOP/etc/terminfo/." "$DIST/usr/share/terminfo/"
    fi

    # init was migrated from etc/init.sh (shell) to sbin/init (C
    # binary) in 5116c3a3.  Prefer the built C binary; only fall back
    # to the shell script if for some reason the C build was skipped.
    if [ -f "$TOP/sbin/init/init" ]; then
        cp "$TOP/sbin/init/init" "$DIST/sbin/init"
    elif [ -f "$TOP/etc/init.sh" ]; then
        cp "$TOP/etc/init.sh" "$DIST/sbin/init"
    else
        echo "Error: no init found ($TOP/sbin/init/init or $TOP/etc/init.sh)" >&2
        exit 1
    fi
    chmod +x "$DIST/sbin/init"

    echo "Installing toolchain to dist/usr/bin..."
    mkdir -p "$DIST/usr/bin"
    # cc, as, ld, and the rest of the C toolchain are provided by the
    # stage-2 binutils + GCC overlay (dist-toolchain/, /tmp/gcc-stage2-staging/).
    # Substrate's earlier hand-rolled usr.bin/cc, usr.bin/as, usr.bin/ld
    # have been retired in favour of the GNU toolchain.
    # Archive / binary utilities that still live under usr.bin/ — these
    # are stand-alone tools, not part of the dropped cc/as/ld set.
    for tool in ar nm size addr2line elfedit readelf; do
        if [ -f "$TOP/usr.bin/$tool/$tool" ]; then
            cp "$TOP/usr.bin/$tool/$tool" "$DIST/usr/bin/"
        fi
    done
    # Everything else under usr.bin/ (ldd, sysctl, ...).  Each subdir
    # named NAME ships a binary at NAME/NAME; this loop catches every
    # one not handled by the explicit toolchain block above.
    for dir in "$TOP/usr.bin"/*/ ; do
        if [ -d "$dir" ]; then
            name=$(basename "$dir")
            if [ -f "$dir/$name" ] && [ ! -f "$DIST/usr/bin/$name" ]; then
                cp "$dir/$name" "$DIST/usr/bin/"
            fi
        fi
    done
    # usr.lib helper libraries (libregex, libexvi, libbc, ...) are
    # DT_NEEDED'd by vi, ex, grep, find, sed, bc, dc, etc.  ld.so
    # searches /lib, /usr/lib, /usr/local/lib so install both flavours
    # of each into /usr/lib (with the bare .so symlink).
    echo "Installing usr.lib helper libraries to dist/usr/lib..."
    for dir in "$TOP/usr.lib"/*/ ; do
        [ -d "$dir" ] || continue
        name=$(basename "$dir")
        if [ -f "$dir/lib$name.a" ]; then
            cp "$dir/lib$name.a" "$DIST/usr/lib/"
        fi
        if [ -f "$dir/lib$name.so.0" ]; then
            cp "$dir/lib$name.so.0" "$DIST/usr/lib/"
            ln -sf "lib$name.so.0" "$DIST/usr/lib/lib$name.so"
        fi
    done

    echo "Installing usr.sbin binaries..."
    mkdir -p "$DIST/usr/sbin"
    for dir in "$TOP/usr.sbin"/*/ ; do
        if [ -d "$dir" ]; then
            name=$(basename "$dir")
            if [ -f "$dir/$name" ]; then
                cp "$dir/$name" "$DIST/usr/sbin/"
            fi
        fi
    done

    echo "Installing sbin binaries..."
    for dir in "$TOP/sbin"/*/ ; do
        if [ -d "$dir" ]; then
            name=$(basename "$dir")
            if [ -f "$dir/$name" ]; then
                cp "$dir/$name" "$DIST/sbin/"
            fi
        fi
    done

    # sbin/sdm is the display manager: it ships sdm.sh (script) + sgreet
    # (Xlib client) rather than a binary named "sdm", so the generic loop
    # above misses it.  Use its Makefile install target, which lands
    # /usr/sbin/sdm, /usr/sbin/sgreet and /etc/X11/Xsession — the exact set
    # /etc/rc.d/60-sdm requires.  Guarded on the contrib X stack having been
    # built (skipped on no-X profiles).  CROSS must be passed so sgreet
    # cross-compiles — otherwise its Makefile (CC=$(CROSS)gcc) falls back to
    # the host gcc, which rejects -march=i486 without -m32 ("CPU you selected
    # does not support x86-64 instruction set") on a fresh/stale tree.
    if [ -d "$TOP/dist-overlay/dist-libX11/usr/lib" ]; then
        echo "Building + installing display manager (sdm + sgreet)..."
        make -C "$TOP/sbin/sdm" \
            CROSS=/opt/substrate/bin/i386-unknown-substrate- >/dev/null
        make -C "$TOP/sbin/sdm" install DESTDIR="$DIST" \
            CROSS=/opt/substrate/bin/i386-unknown-substrate- >/dev/null
    fi

    # contrib overlays — staged trees produced by each contrib/$pkg/build.sh.
    # Inetutils ships /usr/bin/telnet + /usr/libexec/{inetd,telnetd}; OpenSSL
    # ships libssl/libcrypto + the openssl CLI under /usr/{lib,bin}; curl
    # ships /usr/bin/curl + libcurl.  Each is independent — missing one
    # just means that one isn't on the image.
    #
    # Iterate every dist-* tree that exists at $TOP so newly-added
    # ports get picked up without having to update this list.  The
    # toolchain split — binutils stage 2 in dist-toolchain, gcc stage
    # 2 in /tmp/gcc-stage2-staging because gcc's build doesn't honor
    # a single DESTDIR cleanly — gets handled explicitly below.
    for stage in "$TOP"/dist-overlay/dist-*; do
        [ -d "$stage" ] || continue
        name=$(basename "$stage")
        # Skip dist-* directories that aren't main-image contrib outputs.
        case "$name" in
            dist) continue ;;       # the rootfs staging itself
            # Full foreign-OS rootfs trees for the separate FreeBSD/NetBSD
            # personality images (freebsd.img / netbsd.img).  They carry
            # their own /bin, /etc, /rescue, ... and must NOT be overlaid
            # onto the native substrate image (they also collide, e.g.
            # NetBSD's /etc/security file vs the PAM /etc/security dir).
            dist-freebsd|dist-netbsd) continue ;;
        esac
        echo "Overlaying $name from $stage..."
        (cd "$stage" && tar -cf - .) | (cd "$DIST" && tar -xf -)
    done

    # /usr/var -> /var.
    #
    # contrib/dbus was configured with a localstatedir that kept the /usr
    # prefix, so everything built against it looks for the system bus at
    # /usr/var/run/dbus/system_bus_socket instead of /var/run/... .  Under TDE
    # that is a permanent error loop -- kdesktop retries every 4 seconds:
    #   ERROR: Failed to open connection to system message bus: Failed to
    #   connect to socket /usr/var/run/dbus/system_bus_socket
    #
    # The overlay ships /usr/var/{run,lib}/dbus as EMPTY directories (0 files
    # in the tree), so replacing them with a link loses nothing and makes the
    # baked-in path resolve to the real /var.  This has to run after the
    # overlay loop above, because that is what recreates the directory.
    if [ -d "$DIST/usr/var" ] && [ ! -L "$DIST/usr/var" ]; then
        if [ -z "$(find "$DIST/usr/var" -type f -o -type l 2>/dev/null | head -1)" ]; then
            rm -rf "$DIST/usr/var"
            ln -sfn ../var "$DIST/usr/var"
            echo "Linked /usr/var -> /var (dbus localstatedir compat)"
        else
            echo "WARNING: $DIST/usr/var has real files; leaving it alone" >&2
        fi
    fi

    # gcc stage 2 lives in /tmp/gcc-stage2-staging (see contrib/gcc/
    # build.sh — the gcc build doesn't cooperate with a custom
    # DESTDIR the way binutils does, so it stages to /tmp instead of
    # alongside dist-toolchain).  Overlay it the same way.
    GCC_STAGE2_STAGING="${GCC_STAGE2_STAGING:-/tmp/gcc-stage2-staging}"
    if [ -d "$GCC_STAGE2_STAGING" ]; then
        echo "Overlaying contrib/gcc stage 2 from $GCC_STAGE2_STAGING..."
        (cd "$GCC_STAGE2_STAGING" && tar -cf - .) | (cd "$DIST" && tar -xf -)
    else
        echo "  (skipped contrib/gcc stage 2: $GCC_STAGE2_STAGING does not exist)"
    fi

    finalize_x_fonts
    build_man_db

    # /bin/sh -> /usr/bin/zsh.  Substrate's POSIX shell is zsh in
    # sh-emulation mode; the in-tree bin/sh/ is intentionally not
    # built (see CLAUDE.md).  Every shebang line (rc.d scripts,
    # configure, autoconf-generated build scripts, ...) relies on
    # this symlink existing, so create it once contrib/zsh has
    # staged /usr/bin/zsh.
    if [ -f "$DIST/usr/bin/zsh" ]; then
        mkdir -p "$DIST/bin"
        ln -sf /usr/bin/zsh "$DIST/bin/sh"
        echo "Installed /bin/sh -> /usr/bin/zsh"
    else
        echo "  (skipped /bin/sh symlink: $DIST/usr/bin/zsh missing)"
    fi

    echo "Installing target testsuites to /tmp on the image..."
    mkdir -p "$DIST/tmp"
    # Each entry below is "src-dir:binary-name".  The cross-built
    # binary is dropped into $DIST/tmp so users can run /tmp/<name>
    # from a live shell on the target.
    for entry in \
        "tests/lib/sockets:torture_unix" \
        "tests/lib/ipc:torture_ipc" \
        "tests/lib/ipc:torture_pipe" \
        "tests/lib/ipc:torture_sem" \
        "tests/lib/ipc:torture_ksem" \
        "tests/lib/pty:torture_pty" \
        "tests/lib/signal:torture_signal" \
        "tests/lib/futex:torture_futex" \
        "tests/lib/c:torture_fcntl" \
        "tests/lib/c:torture_malloc" \
        "tests/lib/c:torture_procs" \
        "tests/lib/c:torture_evdev" \
        "tests/lib/c:torture_x11_shape"; do
        srcdir="${entry%%:*}"
        binname="${entry##*:}"
        srcbin="$TOP/$srcdir/$binname"
        if [ -f "$srcbin" ]; then
            cp "$srcbin" "$DIST/tmp/$binname"
            chmod +x "$DIST/tmp/$binname"
        else
            echo "  (skip: $srcbin not built)"
        fi
    done

    echo "Installing libraries to dist/usr/lib..."
    # crt0 + crti/crtn (SysV-style startfile bracket) and libsys for linking.
    for crt in crt0.o crti.o crtn.o; do
        [ -f "$TOP/lib/c/$crt" ] && cp "$TOP/lib/c/$crt" "$DIST/usr/lib/"
    done
    [ -f "$TOP/lib/sys/libsys.a" ] && cp "$TOP/lib/sys/libsys.a" "$DIST/usr/lib/"
    [ -f "$TOP/lib/m/libm.a" ] && cp "$TOP/lib/m/libm.a" "$DIST/usr/lib/"
    # elfobj library (needed by ld, as)
    [ -f "$TOP/usr.lib/elfobj/libelfobj.a" ] && cp "$TOP/usr.lib/elfobj/libelfobj.a" "$DIST/usr/lib/"

    echo "Root filesystem prepared in: $DIST"
}

#
# Partition layout.  The image is a real MBR disk, not a bare filesystem,
# so it can be written to a USB stick and booted on BIOS *or* UEFI:
#
#   LBA 0        MBR: GRUB boot.img + the partition table
#   LBA 1..2047  post-MBR gap: GRUB core.img (BIOS path)
#   p1  1 MiB    FAT32, type EF (EFI System Partition), label sub-boot
#                  /EFI/BOOT/BOOT{X64,IA32}.EFI  - UEFI path
#                  /boot/grub/grub.cfg           - menu, shared by both
#   p2  +50 MiB  ext2, label sub-root - the root filesystem, holds /vmunix
#
# p1 starts at LBA 2048 (1 MiB) both because that is the conventional
# alignment for flash media and because it leaves 2047 sectors of gap, which
# GRUB's ~350-sector core.img needs.
#
BOOT_PART_MIB=50
BOOT_PART_LBA=2048                                   # 1 MiB
BOOT_PART_SECTORS=$((BOOT_PART_MIB * 1024 * 2))      # 50 MiB in 512B sectors
ROOT_PART_LBA=$((BOOT_PART_LBA + BOOT_PART_SECTORS))
BOOT_LABEL=sub-boot
ROOT_LABEL=sub-root

# GRUB modules baked into every core image.  part_msdos+fat get us to the
# ESP; ext2 lets GRUB read /vmunix off the root partition; search_label
# implements `search --label`; multiboot/multiboot2 are the two handoff
# protocols (mb1 for BIOS, mb2 for EFI).
# all_video is a meta-module and exists for i386-pc, i386-efi and x86_64-efi
# alike, so one list serves every target.  It is not optional: /vmunix is the
# framebuffer kernel, and without a video driver GRUB answers the multiboot
# framebuffer request with "no suitable video mode found" and hands over a
# console the kernel cannot draw on -- fatal under UEFI, which has no EGA
# text mode to fall back to.
GRUB_MODULES="part_msdos fat ext2 normal configfile search search_label echo test multiboot multiboot2 boot linux gzio serial terminal all_video"

# Emit the shared grub.cfg.  One config serves BIOS and UEFI: $grub_platform
# picks the handoff protocol, and the root filesystem is located by LABEL so
# neither the BIOS drive number nor the UEFI device path has to be known.
write_grub_cfg() {
    cat > "$1" <<EOF
serial --unit=0 --speed=115200
terminal_output serial console
terminal_input serial console
set timeout=5
set default=0

insmod part_msdos
insmod fat
insmod ext2
insmod search_label

# Find the root filesystem by volume label rather than by (hdN,msdosM):
# on a USB stick the drive number depends on what else is plugged in.
search --no-floppy --label $ROOT_LABEL --set=subroot

# One function per handoff protocol so each menu entry is a single line and
# the option list is impossible to get out of step between BIOS and UEFI.
# Every menu entry boots the root read-only.  The kernel's own default is
# read-write -- a direct-kernel boot that passes nothing still comes up rw --
# but a GRUB boot is the one that has a chance to run e2fsck before anything
# has written to the filesystem, so "ro" belongs here rather than in the
# kernel.  Bring it up read-write afterwards with:
#     mount -o remount,rw /
# Uniprocessor by default: the APs are brought up but never scheduled on, so
# SMP buys nothing today and costs a class of bring-up failures that only
# show up on real hardware.
#
# nosmp is baked into this function rather than passed per entry, so every
# menuentry below inherits it.  It cannot be overridden by appending a token,
# because the kernel tests for nosmp with cmdline_has() -- presence, not
# last-one-wins -- so the SMP entry at the bottom of this file repeats the
# multiboot lines instead of calling this function.
function substrate_boot {
    set root=\$subroot
    echo "Loading /vmunix from $ROOT_LABEL ro nosmp \$*"
    if [ "\$grub_platform" = "efi" ]; then
        multiboot2 /vmunix root=LABEL=$ROOT_LABEL ro nosmp \$*
    else
        multiboot /vmunix root=LABEL=$ROOT_LABEL ro nosmp \$*
    fi
    boot
}

menuentry "Substrate" {
    substrate_boot
}

menuentry "Substrate (serial console + verbose)" {
    substrate_boot serial_debug console=serial0
}

# Diagnostic entries.  These exist so the options can be SELECTED rather than
# typed in at the GRUB prompt: an option that never reaches the kernel looks
# exactly like an option that had no effect, and the two were confused for a
# whole debugging round on a Lenovo C460.  Whichever entry is chosen, the
# kernel echoes what it actually received on its "Boot Args:" line -- check
# that line first, always.
menuentry "Substrate (USB bring-up trace)" {
    substrate_boot xhcidebug ehcidebug
}

menuentry "Substrate (no USB BIOS handoff)" {
    substrate_boot nousbhandoff
}

menuentry "Substrate (no USB BIOS handoff + trace)" {
    substrate_boot nousbhandoff xhcidebug ehcidebug
}

# The Intel USB2 reroute is opt-in: it wedges a C460 on legacy BIOS at the
# XUSB2PR write.  Without it the USB2 ports stay on the companion EHCI, which
# drives them fine -- so never pair xhciroute with noehci, or a root
# filesystem on a USB stick disappears and the kernel panics.
menuentry "Substrate (Intel USB2 reroute + trace)" {
    substrate_boot xhciroute xhcidebug ehcidebug
}

# The one entry that does NOT go through substrate_boot, because that function
# hard-codes nosmp and no appended token can undo it.  Selecting this is the
# only way to bring the APs up from the menu.
menuentry "Substrate (SMP)" {
    set root=\$subroot
    echo "Loading /vmunix from $ROOT_LABEL ro (SMP)"
    if [ "\$grub_platform" = "efi" ]; then
        multiboot2 /vmunix root=LABEL=$ROOT_LABEL ro
    else
        multiboot /vmunix root=LABEL=$ROOT_LABEL ro
    fi
    boot
}

EOF
}

create_image() {
    echo "Creating ${IMAGE_SIZE_MIB}MiB bootable disk image: $IMAGE"
    echo "  p1 = ${BOOT_PART_MIB}MiB FAT32 ESP '$BOOT_LABEL' (GRUB, BIOS + UEFI)"
    echo "  p2 = ext2 '$ROOT_LABEL' (root filesystem, /vmunix)"

    # Check if dist directory exists and is not empty
    if [ ! -d "$DIST" ] || [ -z "$(ls -A "$DIST")" ]; then
        echo "Error: dist/ directory is empty. Run with --dist first."
        exit 1
    fi

    for t in sfdisk mkfs.vfat mmd mcopy; do
        command -v "$t" >/dev/null 2>&1 || {
            echo "Error: $t is required to bake the bootable image." >&2; exit 1; }
    done
    # grub-mkimage may come from contrib/grub instead of the host, so only
    # insist on a host copy when the vendored port has not been built.
    if [ ! -x "$TOP/contrib/grub/dist-grub/usr/bin/grub-mkimage" ]; then
        command -v grub-mkimage >/dev/null 2>&1 || {
            echo "Error: grub-mkimage is required to bake the bootable image." >&2
            echo "       Either install GRUB, or build the vendored one:" >&2
            echo "         contrib/grub/fetch.sh && contrib/grub/build.sh" >&2
            exit 1; }
    fi

    # Create the image file
    rm -f "$IMAGE"
    truncate -s "${IMAGE_SIZE_MIB}M" "$IMAGE"

    echo "Writing MBR partition table..."
    sfdisk -q "$IMAGE" >/dev/null <<EOF
label: dos
unit: sectors
start=$BOOT_PART_LBA, size=$BOOT_PART_SECTORS, type=ef, bootable
start=$ROOT_PART_LBA, type=83
EOF

    # 1. Create AND populate the filesystem in one atomic mke2fs -d pass,
    #    under fakeroot so the image is ROOT-OWNED.
    #
    #    Two things this replaces, both of which broke the desktop:
    #    a) The old "empty mkfs + a single `debugfs -w -f cmdfile` batch-write"
    #       cross-links data blocks between unrelated files — it handed
    #       /etc/fonts (inode 17) and a CDE ToolTalk file the SAME block (108),
    #       so fontconfig read CDE data as directory entries and every font
    #       lookup failed ("finddir walk truncated"); a clean rebuild just
    #       reproduced it.  mke2fs -d does correct, conflict-free block
    #       allocation — the result passes `e2fsck -fn` with no multiply-claimed
    #       blocks.
    #    b) mke2fs -d takes ownership from the source files, and dist/ is owned
    #       by the (non-root) build user, so every file would land as uid 1000.
    #       TDE's lnusertemp/ICE refuse temp dirs not owned by root (uid 0), so
    #       DCOP/sycoca never come up and the desktop stays blank.  fakeroot
    #       fakes `chown -R 0:0` (without touching the real dist/) so mke2fs
    #       records uid/gid 0 — matching what the old debugfs path produced.
    #
    #    su(1) and ping(8) are the setuid-root binaries (dist/ has no other
    #    setuid files); chown clears the bit so it is re-set inside the
    #    fakeroot session.  ping needs it because opening a SOCK_RAW socket is
    #    now root-only (audit UDP-07) -- it was already written to drop the
    #    privilege immediately after socket() (CU-PING-01), which only makes
    #    sense for a setuid binary, but the bit was never actually set.
    #    -I 128 prints a harmless post-2038-date warning.  128-byte inodes
    #    are kept because tools/ext2-install-boot and the ext2 boot-block
    #    scheme assume them; see docs/specs/bootloader_ext2_boot.md.
    if ! command -v fakeroot >/dev/null 2>&1; then
        echo "Error: fakeroot is required to bake a root-owned image. Install it." >&2
        exit 1
    fi
    #    -E offset= writes the filesystem into partition 2 in place, so no
    #    multi-GiB temporary file or loop device (i.e. no root) is needed.
    #    The explicit block count is required with -E offset: without it
    #    mke2fs would size the filesystem to the whole image and run off the
    #    end of the partition.  -L stamps the volume label that
    #    LABEL=sub-root resolves against at boot.
    echo "Creating + populating ext2 '$ROOT_LABEL' from $DIST (fakeroot -> root-owned)..."
    root_blocks=$(( (IMAGE_SIZE_MIB * 1024 * 2 - ROOT_PART_LBA) / 2 ))
    fakeroot -- sh -c '
        chown -R 0:0 "$1"
        [ -e "$1/bin/su" ] && chmod 4755 "$1/bin/su"
        [ -e "$1/bin/ping" ] && chmod 4755 "$1/bin/ping"
        mke2fs -F -q -b 1024 -I 128 -O ^resize_inode -L "$4" \
               -E offset=$(( $3 * 512 )) -d "$1" "$2" "$5"
    ' _ "$DIST" "$IMAGE" "$ROOT_PART_LBA" "$ROOT_LABEL" "$root_blocks"

    install_grub

    # GRUB owns the MBR on this partitioned layout: boot.img plus the
    # partition table live at LBA 0.  Anything that writes a stage1 to LBA 0
    # (the old bare-ext2-filesystem scheme) would destroy both; that scheme is
    # no longer part of the image build.  tools/ext2-install-boot still exists
    # for hand-installing a stage1/stage2 pair into a bare ext2 image.

    echo "Image created: $IMAGE"
    sfdisk -l "$IMAGE" 2>/dev/null | tail -4
}

#
# Build the FAT32 ESP and install both GRUB boot paths into it.
#
# BIOS and UEFI need completely different GRUB binaries but share one
# grub.cfg.  Nothing here needs root: the FAT filesystem is built in a
# scratch file and populated with mtools (no mount), and the BIOS MBR/gap
# embedding is done by tools/grub-embed-mbr (grub-bios-setup refuses to
# operate on a plain file -- see that script's header).
#
# Stage a private copy of one of GRUB's EFI module trees and relax the
# relocator's .text so GRUB can still write its own handoff state through
# its W^X enforcement.  Without this every EFI boot of a multiboot kernel
# dies in a firmware page fault before the kernel runs at all -- see
# tools/grub-unprotect-relocator for the full diagnosis.  The system GRUB is
# never touched; grub-mkimage is pointed at the copy with -d.
#
# GRUB provenance.  contrib/grub builds GRUB from a PGP-verified upstream
# tarball and stages it under contrib/grub/dist-grub/usr; prefer that so the
# image does not depend on whatever GRUB the build host happens to have (or
# on it being installed at all).  Falls back to the host paths when the port
# has not been built.
#
grub_setup() {
    local staged="$TOP/contrib/grub/dist-grub/usr"
    if [ -x "$staged/bin/grub-mkimage" ] && [ -d "$staged/lib/grub" ]; then
        GRUB_BIN="$staged/bin"
        GRUB_LIB="$staged/lib/grub"
        GRUB_SRC="contrib/grub (vendored $("$staged/bin/grub-mkimage" --version 2>/dev/null | awk '{print $NF}'))"
    else
        GRUB_BIN=""          # empty: use $PATH
        GRUB_LIB="/usr/lib/grub"
        GRUB_SRC="host"
    fi
}

grub_mkimage() {
    if [ -n "$GRUB_BIN" ]; then "$GRUB_BIN/grub-mkimage" "$@"; else grub-mkimage "$@"; fi
}

efi_moddir() {
    local target="$1" dest="$2"

    mkdir -p "$dest" || return 1
    cp "$GRUB_LIB/$target"/* "$dest"/ 2>/dev/null || return 1
    if [ -f "$dest/relocator.mod" ]; then
        python3 "$TOP/tools/grub-unprotect-relocator" "$dest/relocator.mod" \
            >/dev/null || return 1
    fi
    return 0
}

install_grub() {
    local esp gdir
    esp=$(mktemp -t substrate-esp-XXXXXX.img)
    gdir=$(mktemp -d -t substrate-grub-XXXXXX)
    # shellcheck disable=SC2064
    trap "rm -rf '$esp' '$gdir'" RETURN

    grub_setup
    echo "  GRUB: $GRUB_SRC"

    echo "Building ${BOOT_PART_MIB}MiB FAT32 ESP '$BOOT_LABEL'..."
    truncate -s "${BOOT_PART_MIB}M" "$esp"
    # -n sets BS_VolLab *and* the root-directory volume-label entry, which is
    # what fat_read_label() (and therefore LABEL=sub-boot) actually reads.
    mkfs.vfat -F 32 -n "$BOOT_LABEL" "$esp" >/dev/null 2>&1

    write_grub_cfg "$gdir/grub.cfg"

    mmd -i "$esp" ::/EFI ::/EFI/BOOT ::/boot ::/boot/grub >/dev/null 2>&1
    mcopy -i "$esp" "$gdir/grub.cfg" ::/boot/grub/grub.cfg

    # --- UEFI ---------------------------------------------------------
    # Removable-media path: firmware with no NVRAM boot entry (i.e. a USB
    # stick on a machine that has never seen it) falls back to
    # /EFI/BOOT/BOOT<arch>.EFI.  Ship both arches: x86_64 firmware is the
    # common case, but 32-bit UEFI exists on older tablets/netbooks, which
    # is exactly the hardware a 32-bit OS is interesting on.
    local built_efi=""
    if [ -d "$GRUB_LIB/x86_64-efi" ]; then
        efi_moddir "x86_64-efi" "$gdir/mod-x64" &&
        grub_mkimage -d "$gdir/mod-x64" -O x86_64-efi \
            -o "$gdir/BOOTX64.EFI" -p "/boot/grub" $GRUB_MODULES >/dev/null 2>&1 &&
        mcopy -i "$esp" "$gdir/BOOTX64.EFI" ::/EFI/BOOT/BOOTX64.EFI &&
        built_efi="$built_efi x64"
    fi
    if [ -d "$GRUB_LIB/i386-efi" ]; then
        efi_moddir "i386-efi" "$gdir/mod-ia32" &&
        grub_mkimage -d "$gdir/mod-ia32" -O i386-efi \
            -o "$gdir/BOOTIA32.EFI" -p "/boot/grub" $GRUB_MODULES >/dev/null 2>&1 &&
        mcopy -i "$esp" "$gdir/BOOTIA32.EFI" ::/EFI/BOOT/BOOTIA32.EFI &&
        built_efi="$built_efi ia32"
    fi
    [ -n "$built_efi" ] && echo "  UEFI GRUB:$built_efi"

    # --- BIOS ---------------------------------------------------------
    # core.img goes in the post-MBR gap, not on the ESP: the MBR boot code
    # can only reach it by LBA, having no filesystem driver at that point.
    # The modules are still copied to the ESP so GRUB can load extras and
    # find its config via the baked-in prefix.
    if [ -d "$GRUB_LIB/i386-pc" ]; then
        mmd -i "$esp" ::/boot/grub/i386-pc >/dev/null 2>&1
        mcopy -i "$esp" "$GRUB_LIB"/i386-pc/*.mod ::/boot/grub/i386-pc/ >/dev/null 2>&1
        # Locate the ESP by LABEL, exactly as grub.cfg locates the root
        # filesystem, instead of baking in a drive number.  The prefix used to
        # be a literal "(hd0,msdos1)/boot/grub", which is only correct when the
        # boot medium happens to be BIOS drive 0.  On a machine with internal
        # disks -- a Lenovo C460 has two SATA drives -- a USB stick is not hd0,
        # so core.img went looking for /boot/grub on an internal disk's first
        # partition, found nothing, and dropped to a bare "grub>" prompt with
        # no menu.  An embedded config runs before the prefix is needed and can
        # search for the partition by name; `search` and `search_label` are
        # already in GRUB_MODULES because grub.cfg needs them too.
        cat > "$gdir/early.cfg" <<EOF
search --no-floppy --label $BOOT_LABEL --set=root
set prefix=(\$root)/boot/grub
EOF
        grub_mkimage -O i386-pc -o "$gdir/core.img" \
            -c "$gdir/early.cfg" -p "/boot/grub" \
            biosdisk $GRUB_MODULES >/dev/null 2>&1
        mcopy -i "$esp" "$gdir/core.img" ::/boot/grub/i386-pc/core.img
    fi

    # Splice the finished ESP into partition 1.
    dd if="$esp" of="$IMAGE" bs=512 seek="$BOOT_PART_LBA" conv=notrunc status=none

    if [ -f "$gdir/core.img" ]; then
        python3 "$TOP/tools/grub-embed-mbr" "$IMAGE" \
            --boot-img "$GRUB_LIB/i386-pc/boot.img" \
            --core-img "$gdir/core.img" \
            --first-partition-lba "$BOOT_PART_LBA"
    fi
}

install_bootloader() {
    local stage1="$BOOT_DIR/stage1.bin"
    local stage2="$BOOT_DIR/stage2.bin"

    if [ ! -f "$stage1" ] || [ ! -f "$stage2" ]; then
        build_bootloader
    fi

    echo "Installing Substrate bootloader into $IMAGE..."
    python3 "$TOP/tools/ext2-install-boot" "$IMAGE" "$stage1" "$stage2"
}

# Parse arguments
if [ $# -eq 0 ]; then
    usage
fi

DO_DIST=false
DO_TOOLCHAIN=false
DO_IMAGE=false
DO_BOOT=true

while [[ $# -gt 0 ]]; do
    case $1 in
        --dist)
            DO_DIST=true
            shift
            ;;
        --toolchain)
            DO_TOOLCHAIN=true
            shift
            ;;
        --image)
            DO_IMAGE=true
            shift
            ;;
        --no-boot)
            DO_BOOT=false
            shift
            ;;
        --help)
            usage
            ;;
        *)
            echo "Unknown option: $1"
            usage
            ;;
    esac
done

if [ "$DO_DIST" = true ]; then
    clean_dist
    # Build bootloader first so it's ready for image creation
    if [ "$DO_BOOT" = true ]; then
        build_bootloader
    fi
    build_components
    install_to_dist
fi

if [ "$DO_TOOLCHAIN" = true ]; then
    if [ ! -d "$DIST/usr" ]; then
        echo "Error: dist/usr/ does not exist; run --dist first." >&2
        exit 1
    fi
    overlay_toolchain
fi

if [ "$DO_IMAGE" = true ]; then
    create_image
fi

echo ""
if [ "$DO_DIST" = true ]; then
    echo "Dist contents:"
    du -sh "$DIST"
fi
if [ "$DO_IMAGE" = true ]; then
    echo "Image size:"
    ls -lh "$IMAGE"
fi
