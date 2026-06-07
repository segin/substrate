#!/bin/sh
#
# contrib/cde/fetch.sh — fetch the Common Desktop Environment (cdesktopenv)
# source and apply the substrate build adjustments.
#
# CDE is a git repository, not a release tarball.  We track the
# C23-GCC15-Changes branch because substrate's toolchain is GCC 16 and the
# 30-year-old CDE sources need that branch's modern-compiler fixes to build
# at all.  The modern cdesktopenv build is autotools (autogen.sh +
# configure + make) — imake is no longer used (0 Imakefiles in the tree).
#
# This only prepares the source tree; build.sh drives the cross-build.
# NOTE: full CDE additionally needs several prerequisite ports that are NOT
# yet in substrate — see README.SUBSTRATE.md for the dependency roadmap.

set -eu

BRANCH="C23-GCC15-Changes"
REPO="https://git.code.sf.net/p/cdesktopenv/code"

HERE="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${HERE}/build"
TREE_DIR="${BUILD_DIR}/cdesktopenv"

mkdir -p "${BUILD_DIR}"

if [ ! -d "${TREE_DIR}/.git" ]; then
    [ "${1:-}" = "--no-network" ] && { echo "fetch.sh: source tree missing" >&2; exit 1; }
    echo "==> Cloning CDE (${BRANCH}) — this is a large checkout"
    git clone --depth 1 --branch "${BRANCH}" "${REPO}" "${TREE_DIR}"
fi

CDE="${TREE_DIR}/cde"
[ -d "${CDE}" ] || { echo "fetch.sh: ${CDE} missing after clone" >&2; exit 1; }

echo "==> autogen.sh (generate configure)"
( cd "${CDE}" && ./autogen.sh )

# --- substrate adjustments to the generated autotools files --------------
# config.sub: accept the substrate OS triple.
if ! grep -q 'substrate\*' "${CDE}/config.sub"; then
    echo "==> patching config.sub for substrate"
    sed -i 's/\(\t| sunos \\\)/\t| substrate* \\\n\1/' "${CDE}/config.sub"
fi
# libtool (generated configure): treat substrate* like linux* for shared libs.
if ! grep -q 'substrate\*' "${CDE}/configure"; then
    echo "==> patching libtool host_os cases for substrate"
    sed -i \
      -e 's@linux\* | k\*bsd\*-gnu | kopensolaris\*-gnu | gnu\*)@linux* | k*bsd*-gnu | kopensolaris*-gnu | gnu* | substrate*)@g' \
      -e 's@gnu\* | linux\* | tpf\* | k\*bsd\*-gnu | kopensolaris\*-gnu)@gnu* | linux* | tpf* | k*bsd*-gnu | kopensolaris*-gnu | substrate*)@g' \
      -e 's@^\(\s*\)linux\*)@\1linux* | substrate*)@g' \
      "${CDE}/configure"
fi

echo "==> CDE source ready at ${CDE}"
