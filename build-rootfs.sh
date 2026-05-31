#!/bin/bash
# Build script to create root filesystem and images

set -e

TOP="$(cd "$(dirname "$0")" && pwd)"
DIST="$TOP/dist"
IMAGE="$TOP/rootfs.img"
IMAGE_SIZE_MIB=4096
BOOT_DIR="$TOP/sys/boot"
EXT2BOOT_DIR="$TOP/contrib/ext2-boot"

usage() {
    echo "Usage: $0 [options]"
    echo ""
    echo "Options:"
    echo "  --dist       Prepare the dist/ directory with all distribution files"
    echo "  --toolchain  Overlay contrib stage-2 toolchain staging trees onto dist/usr/"
    echo "               (gcc + binutils built by contrib/build-toolchain.sh --stage=2)"
    echo "  --image      Create a ${IMAGE_SIZE_MIB}MiB ext2 filesystem image (rootfs.img)"
    echo "  --no-boot    Skip ext2-boot bootloader installation"
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
DIST_TOOLCHAIN="${DIST_TOOLCHAIN:-$TOP/dist-toolchain}"
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

build_ext2boot() {
    if [ ! -d "$EXT2BOOT_DIR" ]; then
        echo "Warning: contrib/ext2-boot not found (run: git submodule update --init)"
        return 1
    fi
    echo "Building ext2-boot bootloader..."
    make -C "$EXT2BOOT_DIR" -f Makefile.substrate
}

build_bootloader() {
    echo "Building Substrate bootloader..."
    make -C "$BOOT_DIR"
}

build_components() {
    echo "Building kernel..."
    make -C "$TOP/sys" -j4

    echo "Building libc..."
    make -C "$TOP/lib/c" -j4

    echo "Building runtime libraries..."
    make -C "$TOP/lib/sys" -j4
    make -C "$TOP/lib/m" -j4
    make -C "$TOP/lib/pthread" -j4
    make -C "$TOP/lib/dl" -j4
    make -C "$TOP/lib/edit" -j4

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
    for dir in "$TOP/tests/lib/sockets" "$TOP/tests/lib/ipc" "$TOP/tests/lib/pty" "$TOP/tests/lib/signal" "$TOP/tests/lib/fcntl"; do
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
}

install_to_dist() {
    echo "Installing kernel to dist/boot and dist/vmunix..."
    cp "$TOP/sys/kernel.bin" "$DIST/boot/"
    cp "$TOP/sys/kernel.multiboot" "$DIST/boot/"
    # Install kernel as /vmunix for the ext2 bootloader
    cp "$TOP/sys/kernel.multiboot" "$DIST/vmunix"

    echo "Installing libc + runtime libraries to dist/usr/lib + dist/lib..."
    mkdir -p "$DIST/usr/include"
    cp -r "$TOP/include/"* "$DIST/usr/include/"

    # Every PIE binary substrate ships DT_NEEDED at minimum libc.so.0 +
    # libsys.so.0; getty / login also pull libpthread + libm; gcc on
    # substrate pulls libstdc++ + libgcc_s (overlaid from gcc staging).
    # ld.so searches /lib, /usr/lib, /usr/local/lib — populate both
    # so resolution succeeds whichever path is hit first.
    for libdir in c sys m pthread dl edit resolv; do
        if [ -f "$TOP/lib/$libdir/lib$libdir.a" ]; then
            cp "$TOP/lib/$libdir/lib$libdir.a" "$DIST/usr/lib/"
        fi
        if [ -f "$TOP/lib/$libdir/lib$libdir.so.0" ]; then
            cp "$TOP/lib/$libdir/lib$libdir.so.0" "$DIST/lib/"
            cp "$TOP/lib/$libdir/lib$libdir.so.0" "$DIST/usr/lib/"
            ln -sf "lib$libdir.so.0" "$DIST/lib/lib$libdir.so"
            ln -sf "lib$libdir.so.0" "$DIST/usr/lib/lib$libdir.so"
        fi
    done

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

    # inetutils-telnetd has /usr/bin/login compiled in as the exec
    # target; substrate ships login at /bin/login.  Symlink so the
    # default path resolves without needing a custom -E flag in
    # inetd.conf.
    mkdir -p "$DIST/usr/bin"
    if [ ! -e "$DIST/usr/bin/login" ]; then
        ln -sf ../../bin/login "$DIST/usr/bin/login"
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
    # /etc/rc.d/60-sdm requires.  Guarded on sgreet having been built (it
    # needs the contrib X stack, so it is skipped on no-X profiles).
    if [ -x "$TOP/sbin/sdm/sgreet" ]; then
        echo "Installing display manager (sdm + sgreet)..."
        make -C "$TOP/sbin/sdm" install DESTDIR="$DIST" >/dev/null
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
    for stage in "$TOP"/dist-*; do
        [ -d "$stage" ] || continue
        name=$(basename "$stage")
        # Skip dist-* directories that aren't contrib outputs (none
        # currently, but a guard for future "dist-something-else").
        case "$name" in
            dist) continue ;;       # the rootfs staging itself
        esac
        echo "Overlaying $name from $stage..."
        (cd "$stage" && tar -cf - .) | (cd "$DIST" && tar -xf -)
    done

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
        "tests/lib/pty:torture_pty" \
        "tests/lib/signal:torture_signal" \
        "tests/lib/fcntl:torture_fcntl" \
        "tests/lib/c:torture_malloc" \
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

create_image() {
    echo "Creating ${IMAGE_SIZE_MIB}MiB ext2 filesystem image: $IMAGE"
    
    # Check if dist directory exists and is not empty
    if [ ! -d "$DIST" ] || [ -z "$(ls -A "$DIST")" ]; then
        echo "Error: dist/ directory is empty. Run with --dist first."
        exit 1
    fi

    # Create empty sparse file
    rm -f "$IMAGE"
    truncate -s "${IMAGE_SIZE_MIB}M" "$IMAGE"

    # 1. Create empty ext2 filesystem (no -d population yet)
    mkfs.ext2 -F -b 1024 -I 128 -O ^resize_inode "$IMAGE"

    # 2. Install bootloader into pristine filesystem (gets group 0 blocks)
    if [ "$DO_BOOT" = true ]; then
        install_bootloader
    fi

    # 3. Populate filesystem from dist/ via debugfs
    echo "Populating image from $DIST..."
    local cmdfile
    cmdfile=$(mktemp)

    # Create directories first (sorted so parents come before children)
    (cd "$DIST" && find . -mindepth 1 -type d | sort) | while IFS= read -r d; do
        rel="${d#./}"
        echo "mkdir $rel"
    done >> "$cmdfile"

    # Write regular files
    (cd "$DIST" && find . -mindepth 1 -type f) | while IFS= read -r f; do
        rel="${f#./}"
        echo "write $DIST/$rel $rel"
    done >> "$cmdfile"

    # Create symlinks
    (cd "$DIST" && find . -mindepth 1 -type l) | while IFS= read -r l; do
        rel="${l#./}"
        target=$(readlink "$DIST/$rel")
        echo "symlink $rel $target"
    done >> "$cmdfile"

    debugfs -w -f "$cmdfile" "$IMAGE" > /dev/null 2>&1
    rm -f "$cmdfile"

    # su(1) must be setuid-root so a non-root user can elevate.
    # debugfs write copies the host file's mode/owner, so fix it up
    # here: mode 0104755 = S_IFREG | S_ISUID | rwxr-xr-x, owner root.
    if debugfs -R 'stat /bin/su' "$IMAGE" > /dev/null 2>&1; then
        debugfs -w -R 'sif /bin/su mode 0104755' "$IMAGE" > /dev/null 2>&1
        debugfs -w -R 'sif /bin/su uid 0'        "$IMAGE" > /dev/null 2>&1
        debugfs -w -R 'sif /bin/su gid 0'        "$IMAGE" > /dev/null 2>&1
        echo "su(1) marked setuid-root."
    fi

    echo "Image created: $IMAGE"
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
