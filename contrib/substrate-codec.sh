#!/bin/sh
# contrib/substrate-codec.sh — shared fetch/build helpers for the audio
# codec ports (libogg, libvorbis, libopus, flac, faad2, ...).  These are
# all plain autotools libraries cross-compiled the same way; per-port
# build.sh sources this and calls codec_build with its specifics.
#
#   . "${HERE}/../substrate-codec.sh"
#   codec_fetch <tarball> <url> <sha512> <topdir>
#   codec_build <name> <topdir> [extra configure args...]
#
# Expects: HERE (port dir), SUBSTRATE_TOP, STAGE1_PREFIX.

: "${STAGE1_PREFIX:=/opt/substrate}"
: "${JOBS:=$(nproc 2>/dev/null || echo 4)}"
SR="${STAGE1_PREFIX}/i386-unknown-substrate"
PATH="${STAGE1_PREFIX}/bin:${PATH}"; export PATH

codec_fetch() {  # $1=tarball $2=url $3=sha512 $4=topdir
    _tb="$1"; _url="$2"; _sha="$3"; _top="$4"
    mkdir -p "${HERE}/build"; cd "${HERE}/build"
    if [ ! -f "${_tb}" ]; then
        [ "${NO_NETWORK:-0}" = "1" ] && { echo "fetch: ${_tb} missing" >&2; exit 1; }
        curl -fSL -o "${_tb}" "${_url}"
    fi
    echo "${_sha}  ${_tb}" | sha512sum -c -
    rm -rf "${_top}"; tar xf "${_tb}"
}

codec_build() {  # $1=name $2=topdir ; rest=extra configure args
    _name="$1"; _top="$2"; shift 2
    _dest="${SUBSTRATE_TOP}/dist-overlay/dist-${_name}"
    cd "${HERE}/build/${_top}"
    # substrate-aware config.sub/guess + libtool ELF shared-lib support.
    #
    # This used to copy them out of contrib/binutils/build/binutils-*/ and,
    # because the copy was guarded on that directory existing, did nothing
    # at all when it did not -- which is every CI run that hits the
    # toolchain cache, since build.sh then never fetches binutils.  The
    # port got a pristine config.sub and configure stopped with
    #
    #     Invalid configuration `i386-unknown-substrate':
    #         OS `substrate' not recognized
    #
    # substrate_config_sub_fix prefers binutils when present, patches the
    # tree's own copy when not, and asserts the result accepts the triple.
    . "${SUBSTRATE_TOP}/contrib/substrate-autotools.sh"
    substrate_config_sub_fix "."
    sh "${SUBSTRATE_TOP}/contrib/substrate-libtool-shared.sh" ./configure >/dev/null 2>&1 || true
    export PKG_CONFIG_PATH="${SR}/lib/pkgconfig"
    ./configure --host=i386-unknown-substrate --prefix=/usr \
        --enable-shared --enable-static \
        CC=i386-unknown-substrate-gcc CXX=i386-unknown-substrate-g++ \
        CFLAGS="-march=i486 -mtune=i486 -O2 -g -fno-pie -fno-stack-protector" \
        CXXFLAGS="-march=i486 -mtune=i486 -O2 -g -fno-pie -fno-stack-protector" \
        CPPFLAGS="-I${SR}/include" LDFLAGS="-L${SR}/lib" "$@"
    make -j"${JOBS}"
    rm -rf "${_dest}"; make install DESTDIR="${_dest}"
    find "${_dest}" -name '*.la' -delete
    # Stamp ELFOSABI_SUBSTRATE on the produced shared objects.
    for _so in $(find "${_dest}/usr/lib" -name '*.so.*' -type f 2>/dev/null); do
        printf '\100' | dd of="${_so}" bs=1 seek=7 count=1 conv=notrunc status=none
    done
    # Mirror libs + headers + .pc into the cross sysroot so later ports find them.
    cp -a "${_dest}"/usr/lib/*.so* "${SR}/lib/" 2>/dev/null || true
    cp -an "${_dest}"/usr/include/* "${SR}/include/" 2>/dev/null || true
    cp -a "${_dest}"/usr/lib/pkgconfig/*.pc "${SR}/lib/pkgconfig/" 2>/dev/null || true
    echo "==> ${_name} staged under ${_dest}/usr"
}
