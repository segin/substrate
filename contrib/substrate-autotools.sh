# contrib/substrate-autotools.sh — shared helpers for cross-porting modern
# autotools (libtool 2.x) packages to substrate.  Source from a build.sh:
#   . "$(dirname "$0")/../substrate-autotools.sh"
#   substrate_config_sub_fix "${TREE_DIR}"     # in fetch.sh (after extract)
#   substrate_libtool_fix    "${TREE_DIR}/configure"  # in build.sh (before configure)

# Make a tree's config.sub accept the substrate triple.
#
# Preferred: copy the substrate-patched config.sub/config.guess out of the
# binutils port, which is the one place they are maintained.
#
# Fallback: patch the tree's own copy in place.  binutils' EXTRACTED tree is
# only present if the toolchain was built this run, and once the CI toolchain
# cache started working build.sh skips that entirely -- so every port using
# this helper began failing with "contrib/binutils not fetched" on exactly
# the runs where everything else went right.  Depending on another port's
# build directory was the fragile part; this removes it.
#
# Three OS-list layouts, as in contrib/motif/fetch.sh: the modern undashed
# one-per-line form, the 2015-era dashed list with a sortix token, and the
# older dashed list from before sortix existed.
substrate_config_sub_fix() {
    _tree="$1"
    # ${HERE} (absolute port dir) is set by every contrib build/fetch script.
    _binu="$(ls -d "${HERE}"/../binutils/build/binutils-*/ 2>/dev/null | head -1)"
    if [ -n "${_binu}" ]; then
        for _s in config.sub config.guess; do
            find "${_tree}" -name "${_s}" -exec cp -f "${_binu}/${_s}" {} +
        done
    else
        for _cs in $(find "${_tree}" -name config.sub); do
            grep -q 'substrate\*' "${_cs}" || sed -i \
                -e 's/\(| sortix\* \)/\1| substrate* /' \
                -e 's/\(| -sortix\* \)/\1| -substrate* /' \
                -e 's/^\(\t *| -aos\* | -aros\* \)/\t      | -substrate* \\\n\1/' \
                "${_cs}"
        done
    fi
    # Either way, prove it: a sed that matched nothing is silent, and a
    # missing config.sub means configure will reject the host triple with a
    # message that says nothing about why.
    for _cs in $(find "${_tree}" -name config.sub); do
        if ! sh "${_cs}" i386-unknown-substrate >/dev/null 2>&1; then
            echo "substrate_config_sub_fix: ${_cs} still rejects i386-unknown-substrate" >&2
            return 1
        fi
    done
}

# Teach a generated configure's bundled libtool that host_os=substrate is ELF
# + GNU ld + linux-like, so --enable-shared actually builds shared libraries
# (otherwise substrate falls through every dispatch to static-only).
substrate_libtool_fix() {
    sed -i \
      -e 's/linux\* | k\*bsd\*-gnu | kopensolaris\*-gnu | gnu\*)/linux* | k*bsd*-gnu | kopensolaris*-gnu | gnu* | substrate*)/g' \
      -e 's/gnu\* | linux\* | tpf\* | k\*bsd\*-gnu | kopensolaris\*-gnu)/gnu* | linux* | tpf* | k*bsd*-gnu | kopensolaris*-gnu | substrate*)/g' \
      "$1"
}

# Strip libtool archives + patch each non-symlink .so OSABI byte to
# ELFOSABI_SUBSTRATE (0x40); call after `make install DESTDIR=$1`.
substrate_so_finalize() {
    rm -f "$1"/usr/lib/*.la
    _n=0
    for _so in "$1"/usr/lib/*.so.*; do
        [ -f "${_so}" ] || continue; [ -L "${_so}" ] && continue
        printf '\100' | dd of="${_so}" bs=1 seek=7 count=1 conv=notrunc status=none
        _n=$((_n + 1))
    done
    echo "  OSABI->substrate on ${_n} shared objects"
}

# Assemble a unified sysroot under $1 from the named dist trees ($2..) and
# export PKG_CONFIG so pkg-config returns SYSROOT-prefixed -I/-L (the .pc
# files carry prefix=/usr, i.e. on-target paths, which a cross gcc would
# otherwise read as the build host's /usr — pulling host headers/libs of a
# conflicting version).  Also exports CPPFLAGS/LDFLAGS pointing at the
# sysroot.  Usage: substrate_sysroot "${SR}" glib2 libffi zlib ...
substrate_sysroot() {
    _sr="$1"; shift
    rm -rf "${_sr}"; mkdir -p "${_sr}/usr/lib"
    for _d in "$@"; do
        _st="${SUBSTRATE_TOP}/dist-overlay/dist-${_d}"
        [ -d "${_st}/usr" ] || { echo "substrate_sysroot: dist-${_d} missing — build contrib/${_d} first" >&2; return 1; }
        cp -a "${_st}/usr/." "${_sr}/usr/"
    done
    # substrate core libs + unversioned link names
    for _l in c sys m pthread dl; do
        cp "${SUBSTRATE_TOP}/lib/${_l}/lib${_l}.so.0" "${_sr}/usr/lib/" 2>/dev/null || true
        ln -sf "lib${_l}.so.0" "${_sr}/usr/lib/lib${_l}.so" 2>/dev/null || true
    done
    # libxcb's port ships a stub pthread-stubs.pc (substrate's pthreads are
    # in libc/libpthread, not a separate stub lib); the X .pc Requires chain
    # (x11 -> xcb -> pthread-stubs) needs it or `pkg-config --cflags` fails.
    if [ -f "${SUBSTRATE_TOP}/contrib/libxcb/pkgconfig/pthread-stubs.pc" ]; then
        cp -f "${SUBSTRATE_TOP}/contrib/libxcb/pkgconfig/"*.pc "${_sr}/usr/lib/pkgconfig/" 2>/dev/null || true
    fi
    export PKG_CONFIG_SYSROOT_DIR="${_sr}"
    export PKG_CONFIG_LIBDIR="${_sr}/usr/lib/pkgconfig:${_sr}/usr/share/pkgconfig"
    export CPPFLAGS="-I${_sr}/usr/include${CPPFLAGS:+ ${CPPFLAGS}}"
    export LDFLAGS="-L${_sr}/usr/lib -Wl,-rpath-link,${_sr}/usr/lib -Wl,--copy-dt-needed-entries${LDFLAGS:+ ${LDFLAGS}}"
}
