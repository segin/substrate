#!/bin/sh
#
# contrib/tde/check-eh-frame.sh — assert every staged TDE ELF carries
# PT_GNU_EH_FRAME.
#
# TDE is a large C++ stack and it throws.  Without that segment the unwinder
# cannot find the FDEs through dl_iterate_phdr, so a throw crossing a DSO
# boundary reaches std::terminate even with a catch(...) on the stack -- the
# process aborts instead of handling the error.
#
# This has already bitten the tree once: an untracked gcc `specs` override
# swallowed --eh-frame-hdr, and the whole TDE stack was staged without the
# segment and stayed that way for months (64 of 535 ELFs, including
# libtqt-mt.so and libtqt.so, which every TDE binary links).  Nothing caught
# it because everything still linked and started -- it only showed as an
# abort on the first exception.
#
# Run after building any TDE component.  Exits non-zero if anything is
# missing, and names the files.
#
#   ./check-eh-frame.sh                 # all staged TDE dist trees
#   ./check-eh-frame.sh dist-tdelibs    # just one
set -u

HERE="$(cd "$(dirname "$0")" && pwd)"
TOP="$(cd "${HERE}/../.." && pwd)"
: "${STAGE1_PREFIX:=/opt/substrate}"
READELF="${STAGE1_PREFIX}/bin/i386-unknown-substrate-readelf"
command -v "${READELF}" >/dev/null 2>&1 || READELF=readelf

if [ "$#" -gt 0 ]; then
    trees="$*"
else
    trees="dist-tqtinterface dist-tqt3 dist-dbus-1-tqt dist-tdelibs \
           dist-tdebase dist-tdeutils dist-tdegames dist-tdetoys"
fi

total=0; missing=0
for t in ${trees}; do
    d="${TOP}/dist-overlay/${t}"
    [ -d "${d}" ] || continue
    n=0; m=0
    # Shared objects and anything executable; skip non-ELF (scripts, data).
    for f in $(find "${d}" -type f \( -name '*.so*' -o -perm -u+x \) 2>/dev/null); do
        [ "$(dd if="${f}" bs=1 count=4 2>/dev/null | od -An -tx1 | tr -d ' \n')" \
          = "7f454c46" ] || continue
        n=$((n + 1))
        if ! "${READELF}" -l "${f}" 2>/dev/null | grep -q GNU_EH_FRAME; then
            m=$((m + 1))
            echo "  MISSING PT_GNU_EH_FRAME: ${f#${TOP}/}"
        fi
    done
    printf '%-22s %4d ELFs, %3d missing\n' "${t}" "${n}" "${m}"
    total=$((total + n)); missing=$((missing + m))
done

echo "-------------------------------------------------"
printf 'TOTAL %d ELFs, %d missing PT_GNU_EH_FRAME\n' "${total}" "${missing}"
if [ "${missing}" -ne 0 ]; then
    echo "FAIL: C++ exceptions will abort in the files listed above." >&2
    echo "Check that the gcc specs override still passes --eh-frame-hdr" >&2
    echo "(contrib/gcc/install-specs.sh) and rebuild them." >&2
    exit 1
fi
echo "OK: every staged TDE ELF can unwind."
