#!/bin/sh
#
# contrib/less/build.sh — configure + build + install less for
# substrate.  Produces:
#   /usr/bin/{less,lessecho,lesskey}
#   /usr/share/man/man1/{less.1,lessecho.1,lesskey.1}

set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"
VERSION="692"
TREE_DIR="${HERE}/build/less-${VERSION}"

if [ -z "${SUBSTRATE_TOP:-}" ]; then
    p="${HERE}"
    while [ "${p}" != "/" ] && [ ! -f "${p}/AGENTS.md" ] && [ ! -f "${p}/CLAUDE.md" ]; do
        p=$(dirname "${p}")
    done
    SUBSTRATE_TOP="${p}"
fi
: "${STAGE1_PREFIX:=/opt/substrate}"
: "${DESTDIR:=${SUBSTRATE_TOP}/dist-overlay/dist-less}"
: "${JOBS:=$(nproc 2>/dev/null || echo 4)}"

PATH="${STAGE1_PREFIX}/bin:${PATH}"
export PATH

[ -d "${TREE_DIR}" ] || { echo "build.sh: run ./fetch.sh first" >&2; exit 1; }

cd "${TREE_DIR}"

# less's autoconf-driven configure tries to RUN test binaries for
# some probes; cross-compile blocks those.  Pre-set the relevant
# ac_cv_* cache values for substrate.
export ac_cv_path_install="/usr/bin/install -c"
# Substrate ncurses is built without termcap symbol set in libtinfo —
# everything lives in libncurses.  Disable the tinfo probe so less
# doesn't try to link -ltinfo (which is absent).
export ac_cv_lib_tinfo_tgoto=no
export ac_cv_lib_tinfow_tgoto=no
# substrate has no libpcre / libpcre2; less builds without them and
# uses its own POSIX-regex fallback.
export ac_cv_lib_pcre_pcre_compile=no
export ac_cv_lib_pcre2_8_pcre2_compile_8=no

echo "==> configure"
"${TREE_DIR}/configure" \
    --host=i386-unknown-substrate \
    --prefix=/usr \
    --sysconfdir=/etc \
    --mandir=/usr/share/man \
    --with-regex=posix \
    CFLAGS="-march=i486 -mtune=i486 -O2 -g -fno-pie" \
    LDFLAGS="-fno-pie -Wl,--copy-dt-needed-entries"

echo "==> make -j${JOBS}"
make -j"${JOBS}"

echo "==> install into ${DESTDIR}"
rm -rf "${DESTDIR}"
mkdir -p "${DESTDIR}"
make install DESTDIR="${DESTDIR}"

echo "==> Done.  /usr/bin/{less,lessecho,lesskey} staged under ${DESTDIR}"
