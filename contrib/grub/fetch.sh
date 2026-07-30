#!/bin/sh
# fetch.sh — download + PGP-verify + extract + patch GRUB.  Idempotent.
#
# Tarball SHA256 below was cross-verified against upstream's detached PGP
# signature (grub-2.12.tar.xz.sig) made by the GRUB maintainer:
#
#   Daniel Kiper <dkiper@net-space.pl>
#   RSA key BE5C23209ACDDACEB20DB0A28C8189F1988C2166
#   gpg: Good signature
#
# Re-verify whenever VERSION bumps:
#   gpg --keyserver hkps://keys.openpgp.org \
#       --recv-keys BE5C23209ACDDACEB20DB0A28C8189F1988C2166
#   gpg --verify grub-<v>.tar.xz.sig grub-<v>.tar.xz
set -eu

VERSION="2.12"
TARBALL="grub-${VERSION}.tar.xz"
URL="https://ftp.gnu.org/gnu/grub/${TARBALL}"
SIG_URL="${URL}.sig"
SHA256="f3c97391f7c4eaa677a78e090c7e97e6dc47b16f655f04683ebd37bef7fe0faa"

HERE="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${HERE}/build"
TREE_DIR="${BUILD_DIR}/grub-${VERSION}"

mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

if [ ! -f "${TARBALL}" ]; then
    [ "${1:-}" = "--no-network" ] && { echo "fetch.sh: tarball missing" >&2; exit 1; }
    echo "==> Fetching ${URL}"
    curl -sSLO "${URL}"
    curl -sSLO "${SIG_URL}" || echo "fetch.sh: warning: signature not fetched" >&2
fi

echo "==> Verifying ${TARBALL}"
got="$(sha256sum "${TARBALL}" | cut -d' ' -f1)"
if [ "${got}" != "${SHA256}" ]; then
    echo "fetch.sh: SHA256 mismatch" >&2
    echo "  expected ${SHA256}" >&2
    echo "  got      ${got}" >&2
    exit 1
fi

# Signature check is advisory: it needs the maintainer key in the local
# keyring, which a fresh checkout will not have.  The SHA256 above is the
# hard gate; this only reports.
if [ -f "${TARBALL}.sig" ] && command -v gpg >/dev/null 2>&1; then
    if gpg --verify "${TARBALL}.sig" "${TARBALL}" >/dev/null 2>&1; then
        echo "    PGP signature OK"
    else
        echo "    PGP signature not checked (maintainer key absent from keyring)"
    fi
fi

if [ ! -d "${TREE_DIR}" ]; then
    echo "==> Extracting"
    tar xf "${TARBALL}"
fi

# Apply the patch series, if any.  Idempotent via a stamp per patch.
if [ -f "${HERE}/series" ]; then
    cd "${TREE_DIR}"
    while read -r p; do
        case "${p}" in ''|'#'*) continue ;; esac
        stamp=".applied-${p}"
        [ -f "${stamp}" ] && continue
        echo "==> Applying ${p}"
        patch -p1 < "${HERE}/patches/${p}"
        : > "${stamp}"
    done < "${HERE}/series"
fi

echo "==> grub ${VERSION} ready in ${TREE_DIR}"
