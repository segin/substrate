#!/bin/sh
# contrib/psymp3/fetch.sh — clone PsyMP3 pinned to a release tag, apply patches.
set -eu
REPO="https://github.com/segin/psymp3"
# Pinned to the 1.99.11-RELEASE tag (commit dce4f64547eb7f25c10c257805949b120755d5c4).
TAG="1.99.11-RELEASE"
HERE="$(cd "$(dirname "$0")" && pwd)"; BUILD_DIR="${HERE}/build"; TREE="${BUILD_DIR}/psymp3"
mkdir -p "${BUILD_DIR}"
if [ ! -d "${TREE}/.git" ]; then
    git clone "${REPO}" "${TREE}"
fi
cd "${TREE}"
# Tags always come across the dumb fetch protocol, so a --tags fetch reliably
# brings the pinned release in even when a bare SHA fetch would be refused.
git fetch --tags origin 2>/dev/null || git fetch --all --tags
git cat-file -e "refs/tags/${TAG}^{commit}" 2>/dev/null || git fetch --all --tags
git checkout -f "refs/tags/${TAG}"
git clean -fdx >/dev/null 2>&1 || true
if [ -f "${HERE}/series" ]; then
    while IFS= read -r p; do [ -z "$p" ] && continue; case "$p" in \#*) continue;; esac
        echo "==> applying $p"; git apply "${HERE}/patches/$p"; done < "${HERE}/series"
fi
echo "PsyMP3 checked out at tag ${TAG} ($(git rev-parse --short HEAD))"
