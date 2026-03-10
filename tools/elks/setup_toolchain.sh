#!/bin/sh
set -eu

BINUTILS_VER="${BINUTILS_VER:-69e12ea2c125ff830580abbe4699d35ba002148b}"
GCC_VER="${GCC_VER:-ca893320926dc93552390b892a202e9373d040c0}"
GMP_VER="${GMP_VER:-6.1.2}"
MPFR_VER="${MPFR_VER:-3.1.5}"
MPC_VER="${MPC_VER:-1.0.3}"

PREFIX_DEFAULT="${HOME}/.local/substrate-elks-toolchain"
PREFIX="${PREFIX:-$PREFIX_DEFAULT}"
WORKDIR="${WORKDIR:-$PWD/tools/elks/work}"
JOBS="${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 1)}"

DISTDIR="${WORKDIR}/dist"
BUILDDIR="${WORKDIR}/build"

BINUTILS_DIST="binutils-ia16-${BINUTILS_VER}"
GCC_DIST="gcc-ia16-${GCC_VER}"
GMP_DIST="gmp-${GMP_VER}"
MPFR_DIST="mpfr-${MPFR_VER}"
MPC_DIST="mpc-${MPC_VER}"

have_cmd() {
    command -v "$1" >/dev/null 2>&1
}

fetch() {
    url="$1"
    out="$2"

    mkdir -p "$DISTDIR"
    if have_cmd curl; then
        curl -L --fail --retry 3 -o "$out.tmp" "$url"
    elif have_cmd wget; then
        wget -O "$out.tmp" "$url"
    else
        echo "setup_toolchain: need curl or wget" >&2
        exit 1
    fi
    mv "$out.tmp" "$out"
}

fetch_if_missing() {
    url="$1"
    out="$2"

    if [ ! -f "$out" ]; then
        fetch "$url" "$out"
    fi
}

untar_gz() {
    archive="$1"
    dest_parent="$2"

    rm -rf "$dest_parent"
    mkdir -p "$dest_parent"
    tar -xzf "$archive" -C "$dest_parent" --strip-components=1
}

untar_bz2() {
    archive="$1"
    dest_parent="$2"

    rm -rf "$dest_parent"
    mkdir -p "$dest_parent"
    tar -xjf "$archive" -C "$dest_parent" --strip-components=1
}

mkdir -p "$DISTDIR" "$BUILDDIR" "$PREFIX"

fetch_if_missing \
    "https://github.com/tkchia/binutils-ia16/archive/${BINUTILS_VER}.tar.gz" \
    "$DISTDIR/${BINUTILS_DIST}.tar.gz"
fetch_if_missing \
    "https://github.com/tkchia/gcc-ia16/archive/${GCC_VER}.tar.gz" \
    "$DISTDIR/${GCC_DIST}.tar.gz"
fetch_if_missing \
    "https://ftp.gnu.org/gnu/gmp/${GMP_DIST}.tar.bz2" \
    "$DISTDIR/${GMP_DIST}.tar.bz2"
fetch_if_missing \
    "https://ftp.gnu.org/gnu/mpfr/${MPFR_DIST}.tar.bz2" \
    "$DISTDIR/${MPFR_DIST}.tar.bz2"
fetch_if_missing \
    "https://ftp.gnu.org/gnu/mpc/${MPC_DIST}.tar.gz" \
    "$DISTDIR/${MPC_DIST}.tar.gz"

BINUTILS_SRC="$BUILDDIR/binutils-src"
BINUTILS_BUILD="$BUILDDIR/binutils-build"
GCC_SRC="$BUILDDIR/gcc-src"
GCC_BUILD="$BUILDDIR/gcc-build"

untar_gz "$DISTDIR/${BINUTILS_DIST}.tar.gz" "$BINUTILS_SRC"
sed -i.bak 's/development=.*/development=false/' "$BINUTILS_SRC/bfd/development.sh"
rm -f "$BINUTILS_SRC/bfd/development.sh.bak"

rm -rf "$BINUTILS_BUILD"
mkdir -p "$BINUTILS_BUILD"
cd "$BINUTILS_BUILD"
"$BINUTILS_SRC/configure" \
    --target=ia16-elf \
    --prefix="$PREFIX" \
    --enable-ld=default \
    --enable-gold=yes \
    --enable-targets=ia16-elf \
    --enable-x86-hpa-segelf=yes \
    --disable-gdb \
    --disable-libdecnumber \
    --disable-readline \
    --disable-sim \
    --disable-nls
make -j"$JOBS"
make install

untar_gz "$DISTDIR/${GCC_DIST}.tar.gz" "$GCC_SRC"
untar_bz2 "$DISTDIR/${GMP_DIST}.tar.bz2" "$GCC_SRC/gmp"
untar_bz2 "$DISTDIR/${MPFR_DIST}.tar.bz2" "$GCC_SRC/mpfr"
untar_gz "$DISTDIR/${MPC_DIST}.tar.gz" "$GCC_SRC/mpc"
sed -i.bak \
    's/heapb->min->compare (heapa->min)/heapb->m_min->compare (heapa->m_min)/' \
    "$GCC_SRC/gcc/fibonacci_heap.h"
rm -f "$GCC_SRC/gcc/fibonacci_heap.h.bak"

rm -rf "$GCC_BUILD"
mkdir -p "$GCC_BUILD"
cd "$GCC_BUILD"
LC_ALL=C "$GCC_SRC/configure" \
    --target=ia16-elf \
    --prefix="$PREFIX" \
    --without-headers \
    --enable-languages=c \
    --disable-libssp \
    --without-isl \
    --disable-nls
LC_ALL=C make -j"$JOBS"
make install

cat <<EOF
ELKS ia16 toolchain installed at:
  $PREFIX

Add this to your shell:
  export PATH="$PREFIX/bin:\$PATH"

Detected tools:
  $(command -v "${PREFIX}/bin/ia16-elf-gcc" 2>/dev/null || echo "$PREFIX/bin/ia16-elf-gcc")
  $(command -v "${PREFIX}/bin/ia16-elf-as" 2>/dev/null || echo "$PREFIX/bin/ia16-elf-as")
  $(command -v "${PREFIX}/bin/ia16-elf-ld" 2>/dev/null || echo "$PREFIX/bin/ia16-elf-ld")
EOF
