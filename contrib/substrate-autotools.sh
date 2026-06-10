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
