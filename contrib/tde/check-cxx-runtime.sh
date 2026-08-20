#!/bin/sh
#
# contrib/tde/check-cxx-runtime.sh — assert every staged C++ ELF links the
# SHARED libstdc++.
#
# Companion to check-eh-frame.sh, and it exists for the same reason: a silent
# link-time fallback that leaves everything apparently working.
#
# ld resolves -lstdc++ by trying libstdc++.so first and libstdc++.a second.
# For most of this project's life the cross sysroot had only the versioned
# libstdc++.so.6 / .so.6.0.35 and no `libstdc++.so` link name, so every C++
# link fell through to the archive and SUCCEEDED.  The previous TDE staging
# went out as 125 shared libraries each carrying a private copy of the C++
# runtime -- its own operator new/delete, its own iostream and locale globals,
# its own typeinfo objects.  Nothing caught it, because everything linked and
# started; it shows up later as exceptions not matching their handlers across
# a DSO boundary and as duplicated runtime state.
#
# A C++ ELF is one that exports or references Itanium-ABI mangled symbols
# (_Z...).  Pure C objects are skipped -- they legitimately have no libstdc++
# dependency.
#
# Run after building any TDE component.  Exits non-zero if anything links the
# runtime statically, and names the files.
#
#   ./check-cxx-runtime.sh                 # all staged TDE dist trees
#   ./check-cxx-runtime.sh dist-tdelibs    # just one
set -u

HERE="$(cd "$(dirname "$0")" && pwd)"
TOP="$(cd "${HERE}/../.." && pwd)"
: "${STAGE1_PREFIX:=/opt/substrate}"
READELF="${STAGE1_PREFIX}/bin/i386-unknown-substrate-readelf"
command -v "${READELF}" >/dev/null 2>&1 || READELF=readelf

if [ "$#" -gt 0 ]; then
    TREES=""
    for a in "$@"; do TREES="${TREES} ${TOP}/dist-overlay/${a}"; done
else
    TREES="$(echo "${TOP}"/dist-overlay/dist-tde* "${TOP}"/dist-overlay/dist-tqt*)"
fi

total=0; cxx=0; bad=0
for tree in ${TREES}; do
    [ -d "${tree}" ] || continue
    # -type f: skip the symlink farm, so each real object is judged once.
    for f in $(find "${tree}" -type f \( -name '*.so' -o -name '*.so.*' -o -perm -u+x \) 2>/dev/null); do
        # ELF magic, cheaply.
        case "$(dd if="${f}" bs=4 count=1 2>/dev/null | tr -d '\0')" in
            *ELF*) ;;
            *) continue ;;
        esac
        total=$((total + 1))
        "${READELF}" -sW "${f}" 2>/dev/null | grep -q ' _Z' || continue
        cxx=$((cxx + 1))
        if ! "${READELF}" -dW "${f}" 2>/dev/null | grep -q 'NEEDED.*libstdc++\.so'; then
            bad=$((bad + 1))
            echo "STATIC libstdc++: ${f#${TOP}/}"
        fi
    done
done

echo "check-cxx-runtime: ${total} ELF(s), ${cxx} C++, ${bad} linking libstdc++ statically"
if [ "${bad}" -ne 0 ]; then
    echo ""
    echo "Those objects each carry a private C++ runtime.  Usually the cause is"
    echo "a missing link name in the cross sysroot -- check:"
    echo "    ls -l ${STAGE1_PREFIX}/i386-unknown-substrate/lib/libstdc++.so"
    echo "and if it is absent, run contrib/gcc/install-specs.sh to restore it,"
    echo "then rebuild.  (ld falls back to libstdc++.a with no diagnostic.)"
    exit 1
fi
exit 0
