#!/bin/sh
#
# strip-staging.sh — strip substrate-target binaries in a stage-2 staging tree.
#
# The stage-2 toolchain is compiled with `-g -O2`, and GCC's install targets
# never strip.  Unstripped, the four big compiler binaries alone dominate the
# root filesystem:
#
#   cc1plus   386 MB      lto1      356 MB
#   cc1       367 MB      lto-dump  356 MB
#
# ~1.4 GB of a 4 GiB image, essentially all DWARF.  Nothing on the target
# reads it: there is no source tree next to the compiler to resolve it
# against, and a crash in cc1 is debugged on the build host against the
# build tree, not on the guest.
#
# Usage:  strip-staging.sh <staging-root> [label]
#
# Env:
#   STAGE1_PREFIX   cross toolchain prefix (default /opt/substrate)
#   TARGET_TRIPLE   default i386-unknown-substrate
#
# What gets stripped, and why the two modes differ:
#
#   executables     --strip-all.  Drops .symtab and .debug_*; neither is
#                   needed to run, and these are the files that matter for
#                   size.
#   shared objects  --strip-debug, NOT --strip-all.  A .so needs .dynsym to
#                   resolve at load time.  GNU strip does keep .dynsym under
#                   --strip-all (it is SHF_ALLOC), but --strip-debug states
#                   the intent and matches what install-stripped-to-rootfs.sh
#                   and build-libstdcxx-shared.sh already do.
#
# Static archives (.a) are deliberately left alone: stripping their DWARF
# would save ~40 MB but takes away the ability to step into libstdc++ from a
# target-side debugger, which is a capability trade rather than dead weight.
#
# Must use the TARGET strip.  The host strip can usually read i386 ELF, but
# the target one is the tool that matches these objects and is guaranteed
# present -- stage 2 cannot have been built without it.

set -eu

STAGE_ROOT="${1:-}"
LABEL="${2:-stage-2}"

[ -n "$STAGE_ROOT" ] || { echo "usage: $0 <staging-root> [label]" >&2; exit 2; }
[ -d "$STAGE_ROOT" ] || { echo "$0: no such staging tree: $STAGE_ROOT" >&2; exit 1; }

: "${STAGE1_PREFIX:=/opt/substrate}"
: "${TARGET_TRIPLE:=i386-unknown-substrate}"

STRIP="${STAGE1_PREFIX}/bin/${TARGET_TRIPLE}-strip"
READELF="${STAGE1_PREFIX}/bin/${TARGET_TRIPLE}-readelf"

for t in "$STRIP" "$READELF"; do
    [ -x "$t" ] || { echo "$0: missing $t (build binutils stage 1 first)" >&2; exit 1; }
done

echo "==> Stripping ${LABEL} binaries in ${STAGE_ROOT}"

before_total=0
after_total=0
count=0

# -perm -u+x alone would also match shell wrappers and libtool scripts; the
# readelf probe is what actually decides, and it costs one exec per candidate.
for f in $(find "$STAGE_ROOT" -type f \( -perm -u+x -o -name '*.so' -o -name '*.so.*' \) | sort); do
    [ -f "$f" ] || continue
    "$READELF" -h "$f" >/dev/null 2>&1 || continue

    case "$("$READELF" -h "$f" 2>/dev/null | sed -n 's/^ *Type: *\([A-Z]*\).*/\1/p')" in
        DYN)  case "$f" in
                  *.so|*.so.*) mode=--strip-debug ;;   # library: keep .dynsym
                  *)           mode=--strip-all   ;;   # PIE executable
              esac ;;
        EXEC) mode=--strip-all ;;
        *)    continue ;;                              # REL/CORE: not ours
    esac

    before=$(stat -c %s "$f")
    "$STRIP" "$mode" "$f" 2>/dev/null || continue
    after=$(stat -c %s "$f")

    before_total=$((before_total + before))
    after_total=$((after_total + after))
    count=$((count + 1))

    # Only narrate the ones worth looking at; the long tail is noise.
    if [ "$((before - after))" -gt 5000000 ]; then
        printf '    %-52s %6d MB -> %5d MB\n' \
            "$(basename "$f")" "$((before / 1048576))" "$((after / 1048576))"
    fi
done

echo "    ${count} binaries: $((before_total / 1048576)) MB -> $((after_total / 1048576)) MB (saved $(((before_total - after_total) / 1048576)) MB)"
