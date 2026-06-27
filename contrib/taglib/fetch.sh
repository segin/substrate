#!/bin/sh
# contrib/taglib/fetch.sh — fetch TagLib 2.0.2 (+ its bundled utfcpp), verify,
# extract, apply patches.
#
# TagLib 2.x needs the utfcpp (utf8cpp) header-only library.  The release
# tarball ships an EMPTY 3rdparty/utfcpp/ directory (it is a git submodule, not
# vendored).  v2.0.2 pins utfcpp at commit df857efc...; we fetch that exact
# commit's tarball and populate 3rdparty/utfcpp/ so TagLib's
# add_subdirectory("3rdparty/utfcpp") fallback (which provides the utf8::cpp
# target) builds offline with no network and no `git submodule update`.
set -eu

VERSION="2.0.2"
TARBALL="taglib-${VERSION}.tar.gz"
URL="https://taglib.github.io/releases/${TARBALL}"
URL_FALLBACK="https://github.com/taglib/taglib/releases/download/v${VERSION}/${TARBALL}"
SHA256="0de288d7fe34ba133199fd8512f19cc1100196826eafcb67a33b224ec3a59737"

# utfcpp submodule pinned by TagLib 2.0.2 (.gitmodules + tree @ v2.0.2).
UTFCPP_COMMIT="df857efc5bbc2aa84012d865f7d7e9cccdc08562"
UTFCPP_TARBALL="utfcpp-${UTFCPP_COMMIT}.tar.gz"
UTFCPP_URL="https://github.com/nemtrif/utfcpp/archive/${UTFCPP_COMMIT}.tar.gz"
UTFCPP_SHA256="911ff4f13cc7bfece2b5f65e7468b962db19c9727f89d560b2617360af08f538"

HERE="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${HERE}/build"
TREE="${BUILD_DIR}/taglib-${VERSION}"
mkdir -p "${BUILD_DIR}"; cd "${BUILD_DIR}"

# --- TagLib tarball ---------------------------------------------------------
[ -f "${TARBALL}" ] || {
  [ "${1:-}" = "--no-network" ] && { echo "missing ${TARBALL}" >&2; exit 1; }
  curl -fSL -o "${TARBALL}" "${URL}" || curl -fSL -o "${TARBALL}" "${URL_FALLBACK}"
}
echo "${SHA256}  ${TARBALL}" | sha256sum -c -

# --- utfcpp tarball (bundled 3rdparty) -------------------------------------
[ -f "${UTFCPP_TARBALL}" ] || {
  [ "${1:-}" = "--no-network" ] && { echo "missing ${UTFCPP_TARBALL}" >&2; exit 1; }
  curl -fSL -o "${UTFCPP_TARBALL}" "${UTFCPP_URL}"
}
echo "${UTFCPP_SHA256}  ${UTFCPP_TARBALL}" | sha256sum -c -

# --- extract ----------------------------------------------------------------
[ -d "${TREE}" ] || tar xf "${TARBALL}"

# Populate the empty 3rdparty/utfcpp submodule dir with the pinned utfcpp tree
# (utfcpp ships its own CMakeLists.txt defining the utf8::cpp INTERFACE target).
if [ ! -f "${TREE}/3rdparty/utfcpp/CMakeLists.txt" ]; then
  tar xf "${UTFCPP_TARBALL}"
  rm -rf "${TREE}/3rdparty/utfcpp"
  mv "utfcpp-${UTFCPP_COMMIT}" "${TREE}/3rdparty/utfcpp"
fi

# --- patches ----------------------------------------------------------------
if [ -f "${HERE}/series" ]; then
  cd "${TREE}"
  while IFS= read -r p; do
    [ -z "$p" ] && continue
    case "$p" in \#*) continue;; esac
    [ -f ".applied-$p" ] || { patch -p1 < "${HERE}/patches/$p"; touch ".applied-$p"; }
  done < "${HERE}/series"
fi

echo "taglib ${VERSION} ready (utfcpp ${UTFCPP_COMMIT})"
