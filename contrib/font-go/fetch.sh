#!/bin/sh
# contrib/font-go/fetch.sh — the Go fonts (2.010), from the golang.org/x/image
# module at v0.46.0.
#
# Taken from the Go module proxy rather than a GitHub archive: proxy zips
# are immutable, whereas GitHub generates commit archives on the fly with
# no promise they stay byte-identical.  The SHA-256 below is of the proxy's
# zip, whose contents hash to the h1: entry the public Go checksum database
# (sum.golang.org) records for golang.org/x/image v0.46.0:
#     h1:b1+oYj0Jbp6K5MDT4i4/eZpYlk3V8SJhhDKh6LBHAyQ=
# The fonts live in font/gofont/ttfs/; they have not changed since 2.010,
# so any later module version carries the same files.
set -eu

VERSION="v0.46.0"
ZIP="x-image-${VERSION}.zip"
URL="https://proxy.golang.org/golang.org/x/image/@v/${VERSION}.zip"
SHA256="10459a17c3533cc9ca4d1ac7a86674bf0c0514b7bac721531b3c1178cfcda82e"

HERE="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${HERE}/build"
mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

if [ ! -f "${ZIP}" ]; then
    [ "${1:-}" = "--no-network" ] && { echo "fetch.sh: ${ZIP} missing" >&2; exit 1; }
    echo "==> Fetching ${URL}"
    if command -v curl >/dev/null 2>&1; then
        curl -fSL --retry 3 --retry-delay 3 --retry-all-errors -o "${ZIP}" "${URL}"
    else
        wget -O "${ZIP}" "${URL}"
    fi
fi

echo "==> Verifying ${ZIP}"
echo "${SHA256}  ${ZIP}" | sha256sum -c -

# Only the fonts and the licence files are needed out of the module.
# Extracted with Python rather than unzip(1), which the CI image's package
# list does not include; python3 it does.
M="golang.org/x/image@${VERSION}"
if [ ! -d "${M}" ]; then
    python3 - "${ZIP}" "${M}" <<'EOF'
import sys, zipfile
zip_path, prefix = sys.argv[1], sys.argv[2]
want = (prefix + "/font/gofont/ttfs/", prefix + "/LICENSE", prefix + "/PATENTS")
with zipfile.ZipFile(zip_path) as z:
    for name in z.namelist():
        if name.startswith(want[0]) or name in want[1:]:
            z.extract(name)
EOF
fi
echo "==> Go fonts ready"
