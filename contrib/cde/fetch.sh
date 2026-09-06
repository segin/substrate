#!/bin/sh
#
# contrib/cde/fetch.sh — fetch the Common Desktop Environment (cdesktopenv)
# source, apply the substrate patch series, and generate the build system.
#
# CDE ships as a git repository rather than a release tarball, so the
# reproducibility anchor is a pinned commit instead of a tarball SHA-256.  We
# track the C23-GCC15-Changes branch: substrate's toolchain is GCC 16, whose
# C23 default rejects the empty-paren prototypes the 30-year-old CDE sources
# are full of, and that branch is upstream's fix for exactly that.
#
# The modern cdesktopenv build is autotools (autogen.sh + configure + make);
# imake is gone (0 Imakefiles remain), which is what makes cross-compiling
# tractable at all.
#
# Order matters here.  The patch series edits configure.ac and Makefile.am,
# so it is applied BEFORE autogen.sh — the generated configure and Makefiles
# then come out already correct, and build.sh does not have to sed them back
# into shape after the fact.  Only the substrate host-triple fixups run after
# autogen.sh, because config.sub and libtool's host_os cases are generated.

set -eu

PIN="68cae0c36b5caccb3e00b5371f4031147154acf9"
BRANCH="C23-GCC15-Changes"
REPO="https://git.code.sf.net/p/cdesktopenv/code"

HERE="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${HERE}/build"
TREE_DIR="${BUILD_DIR}/cdesktopenv"
CDE="${TREE_DIR}/cde"

mkdir -p "${BUILD_DIR}"

if [ ! -d "${TREE_DIR}/.git" ]; then
    [ "${1:-}" = "--no-network" ] && { echo "fetch.sh: source tree missing" >&2; exit 1; }
    echo "==> Cloning CDE ${BRANCH} — this is a large checkout"
    git clone --depth 1 --branch "${BRANCH}" "${REPO}" "${TREE_DIR}"
fi

# Pin.  A shallow clone of the branch tip is the fast path; if upstream has
# moved on, deepen and check the recorded commit out so the patch series still
# applies to the tree it was written against.
if [ "$(git -C "${TREE_DIR}" rev-parse HEAD)" != "${PIN}" ]; then
    echo "==> branch tip is not the pinned commit — fetching ${PIN}"
    git -C "${TREE_DIR}" fetch --unshallow 2>/dev/null || git -C "${TREE_DIR}" fetch --all
    git -C "${TREE_DIR}" checkout --quiet "${PIN}"
fi
echo "==> CDE at $(git -C "${TREE_DIR}" rev-parse --short HEAD)"

[ -d "${CDE}" ] || { echo "fetch.sh: ${CDE} missing after clone" >&2; exit 1; }

# --- substrate patch series ------------------------------------------------
if [ -f "${HERE}/series" ]; then
    cd "${CDE}"
    while IFS= read -r p; do
        case "$p" in
            ''|'#'*) continue ;;
        esac
        if [ ! -f "${HERE}/patches/${p}" ]; then
            echo "fetch.sh: missing patch ${p}" >&2
            exit 1
        fi
        if patch -p1 --dry-run -s -R < "${HERE}/patches/${p}" >/dev/null 2>&1; then
            echo "==> Patch ${p} already applied"
            continue
        fi
        echo "==> Applying ${p}"
        patch -p1 < "${HERE}/patches/${p}"
    done < "${HERE}/series"
fi

# --- generate the build system ---------------------------------------------
echo "==> autogen.sh (generate configure)"
( cd "${CDE}" && ./autogen.sh )

# --- substrate host triple, in the GENERATED files -------------------------
# config.sub and libtool's host_os case arms come out of autoconf/libtool, so
# they cannot be carried as patches against the source tree.
#
# autogen.sh regenerates config.sub from whatever automake the BUILD HOST has,
# so its layout is not ours to predict.  The sed here used to anchor on a
# literal "\t| sunos \\" line; that exists in automake 1.18's copy and not in
# the one Ubuntu 24.04 ships, so on a runner it matched nothing, said nothing,
# and configure failed much later with
#
#   Invalid configuration `i386-unknown-substrate': OS `substrate' not
#   recognized
#
# substrate_config_sub_fix knows the three layouts in circulation and, more
# importantly, asserts afterwards by running config.sub on the target triple.
# This is the third port to hit the same bug (motif and the shared helper were
# the others), so use the shared implementation rather than a fourth sed.
echo "==> teaching config.sub the substrate OS"
. "${HERE}/../substrate-autotools.sh"
substrate_config_sub_fix "${CDE}"
# libtool decides shared-library behaviour from a long series of host_os case
# statements.  Rather than match their exact alternative lists — which differ
# between libtool releases, and silently stop matching when a new OS is added
# to one — add substrate* to every case arm that offers plain `linux*` as an
# alternative.  `linux*` has to be a whole alternative: arms like
# `mips64*-*linux*)` or `linux*android*)` name something more specific and are
# left alone.
if ! grep -q '| substrate\*)' "${CDE}/configure"; then
    echo "==> teaching libtool that substrate* behaves like linux*"
    perl -i -pe '
        if (/^\s*[^#]*\)\s*$/ && /(?:^|\|)\s*linux\*\s*(?:\||\))/ && !/substrate/) {
            s/\)\s*$/ | substrate*)\n/;
        }
    ' "${CDE}/configure"
    sh -n "${CDE}/configure" || { echo "fetch.sh: libtool fixup broke configure" >&2; exit 1; }
fi

echo "==> CDE source ready at ${CDE}"
