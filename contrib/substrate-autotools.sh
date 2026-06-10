# contrib/substrate-autotools.sh — shared helpers for cross-porting modern
# autotools (libtool 2.x) packages to substrate.  Source from a build.sh:
#   . "$(dirname "$0")/../substrate-autotools.sh"
#   substrate_config_sub_fix "${TREE_DIR}"     # in fetch.sh (after extract)
#   substrate_libtool_fix    "${TREE_DIR}/configure"  # in build.sh (before configure)

# Replace a tree's config.sub/config.guess with the substrate-patched pair
# from the binutils port (the substrate triple is unknown to upstream copies).
substrate_config_sub_fix() {
    _tree="$1"
    # ${HERE} (absolute port dir) is set by every contrib build/fetch script.
    _binu="$(ls -d "${HERE}"/../binutils/build/binutils-*/ 2>/dev/null | head -1)"
    [ -n "${_binu}" ] || { echo "substrate_config_sub_fix: contrib/binutils not fetched" >&2; return 1; }
    for _s in config.sub config.guess; do
        find "${_tree}" -name "${_s}" -exec cp -f "${_binu}/${_s}" {} +
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
        _st="${SUBSTRATE_TOP}/dist-${_d}"
        [ -d "${_st}/usr" ] || { echo "substrate_sysroot: dist-${_d} missing — build contrib/${_d} first" >&2; return 1; }
        cp -a "${_st}/usr/." "${_sr}/usr/"
    done
    # substrate core libs + unversioned link names
    for _l in c sys m pthread dl; do
        cp "${SUBSTRATE_TOP}/lib/${_l}/lib${_l}.so.0" "${_sr}/usr/lib/" 2>/dev/null || true
        ln -sf "lib${_l}.so.0" "${_sr}/usr/lib/lib${_l}.so" 2>/dev/null || true
    done
    export PKG_CONFIG_SYSROOT_DIR="${_sr}"
    export PKG_CONFIG_LIBDIR="${_sr}/usr/lib/pkgconfig:${_sr}/usr/share/pkgconfig"
    export CPPFLAGS="-I${_sr}/usr/include${CPPFLAGS:+ ${CPPFLAGS}}"
    export LDFLAGS="-L${_sr}/usr/lib -Wl,-rpath-link,${_sr}/usr/lib -Wl,--copy-dt-needed-entries${LDFLAGS:+ ${LDFLAGS}}"
}
