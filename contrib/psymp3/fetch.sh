#!/bin/sh
# contrib/psymp3/fetch.sh — clone PsyMP3 pinned to a specific commit, apply patches.
set -eu
REPO="https://github.com/segin/psymp3"
COMMIT="5295f5cef7602ac8a2290ff95874d0a04b078138"
HERE="$(cd "$(dirname "$0")" && pwd)"; BUILD_DIR="${HERE}/build"; TREE="${BUILD_DIR}/psymp3"
mkdir -p "${BUILD_DIR}"
if [ ! -d "${TREE}/.git" ]; then
    git clone "${REPO}" "${TREE}"
fi
cd "${TREE}"
# GitHub refuses to serve an arbitrary commit by SHA over the dumb fetch
# protocol unless uploadpack.allowReachableSHA1InWant is set, so the
# SHA-specific fetch usually fails — fall back to fetching all refs, which
# always brings the pinned commit in as it is an ancestor of a branch.
git fetch origin "${COMMIT}" 2>/dev/null || git fetch --all --tags
git cat-file -e "${COMMIT}^{commit}" 2>/dev/null || git fetch origin
git checkout -f "${COMMIT}"
git clean -fdx >/dev/null 2>&1 || true
if [ -f "${HERE}/series" ]; then
    while IFS= read -r p; do [ -z "$p" ] && continue; case "$p" in \#*) continue;; esac
        echo "==> applying $p"; git apply "${HERE}/patches/$p"; done < "${HERE}/series"
fi
echo "PsyMP3 checked out at ${COMMIT}"
