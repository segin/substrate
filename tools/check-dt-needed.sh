#!/bin/sh
#
# tools/check-dt-needed.sh — verify every DT_NEEDED in a staged root resolves.
#
# The image verification used to check that a handful of NAMED files existed.
# That passed while the image was missing eight runtime libraries, because
# nothing asked the stronger question: can every binary we ship actually find
# what it links against?  A booted CI image answered it for us --
#
#     Substrate init starting (pid 1)
#     ld.so: main-program needs librt.so.0 - not found
#     ld.so: fatal: DT_NEEDED resolution failed
#
# -- which is a bad place to learn it.  This asks at bake time instead.
#
# Usage:  check-dt-needed.sh <staged-root> [--strict]
#
#   default   report unresolved libraries and exit 0
#   --strict  exit 1 if anything is unresolved
#
# Env:
#   STAGE1_PREFIX  cross toolchain prefix (default /opt/substrate)
#   TARGET_TRIPLE  default i386-unknown-substrate
#
# The search path mirrors sbin/ld.so: the built-in trusted directories /lib,
# /usr/lib and /usr/local/lib, plus every directory named by /etc/ld.so.conf
# and /etc/ld.so.conf.d/*.conf.  That is what makes CDE (/usr/dt/lib) and TDE
# (/opt/trinity/lib) resolve, so the checker has to read them too rather than
# assume the trusted three.

set -eu

ROOT="${1:-}"
STRICT=0
[ "${2:-}" = "--strict" ] && STRICT=1

[ -n "$ROOT" ] || { echo "usage: $0 <staged-root> [--strict]" >&2; exit 2; }
[ -d "$ROOT" ] || { echo "$0: no such staged root: $ROOT" >&2; exit 1; }

: "${STAGE1_PREFIX:=/opt/substrate}"
: "${TARGET_TRIPLE:=i386-unknown-substrate}"
READELF="${STAGE1_PREFIX}/bin/${TARGET_TRIPLE}-readelf"
[ -x "$READELF" ] || READELF=readelf

# --- where ld.so will look -------------------------------------------------
SEARCH="/lib /usr/lib /usr/local/lib"
for conf in "$ROOT/etc/ld.so.conf" "$ROOT"/etc/ld.so.conf.d/*.conf; do
    [ -f "$conf" ] || continue
    # Same subset ld_load.c parses: one path per line, '#' comments, blanks.
    while IFS= read -r line || [ -n "$line" ]; do
        case "$line" in ''|'#'*|include*) continue ;; esac
        SEARCH="$SEARCH $line"
    done < "$conf"
done

# --- what is available, by SONAME ------------------------------------------
# Keyed on SONAME, not file name: a library resolves by the name recorded in
# its DT_SONAME, and the two differ often enough to matter.
AVAIL="$(mktemp)"
trap 'rm -f "$AVAIL" "$NEEDED" "$MISSING"' EXIT
for d in $SEARCH; do
    [ -d "$ROOT$d" ] || continue
    for f in "$ROOT$d"/*; do
        # A symlink here is available under its own name -- record that and
        # do not read through it.  These are target-absolute (/bin/sh ->
        # /usr/bin/zsh), so following one on the build host reads the HOST's
        # file: that is how an ad-hoc version of this scan "found" /bin/sh
        # linking glibc, when it is a correct symlink to substrate's zsh.
        if [ -L "$f" ]; then
            basename "$f"
            continue
        fi
        [ -f "$f" ] || continue
        basename "$f"
        soname=$("$READELF" -d "$f" 2>/dev/null | sed -n 's/.*SONAME.*\[\(.*\)\].*/\1/p')
        [ -n "$soname" ] && echo "$soname"
    done
done | sort -u > "$AVAIL"

# --- what is wanted --------------------------------------------------------
NEEDED="$(mktemp)"
: > "$NEEDED"
count=0
for f in $(find "$ROOT" -type f \( -perm -u+x -o -name '*.so' -o -name '*.so.*' \) 2>/dev/null); do
    [ -f "$f" ] || continue
    head -c4 "$f" 2>/dev/null | grep -q ELF || continue
    n=$("$READELF" -d "$f" 2>/dev/null | sed -n 's/.*NEEDED.*\[\(.*\)\].*/\1/p') || continue
    [ -n "$n" ] || continue
    count=$((count + 1))
    for lib in $n; do
        printf '%s\t%s\n' "$lib" "${f#"$ROOT"}"
    done
done >> "$NEEDED"

# --- what is wanted but absent ---------------------------------------------
MISSING="$(mktemp)"
cut -f1 "$NEEDED" | sort -u | while IFS= read -r lib; do
    grep -qxF "$lib" "$AVAIL" || echo "$lib"
done > "$MISSING"

echo "==> DT_NEEDED check: $count ELF objects under $ROOT"
echo "    search path: $SEARCH"

if [ ! -s "$MISSING" ]; then
    echo "    all DT_NEEDED libraries resolve."
    exit 0
fi

echo "    UNRESOLVED:" >&2
while IFS= read -r lib; do
    # Name one consumer -- enough to identify the port responsible.
    who=$(grep -m1 -P "^\Q$lib\E\t" "$NEEDED" 2>/dev/null | cut -f2) || who=""
    [ -n "$who" ] || who=$(awk -F'\t' -v l="$lib" '$1==l {print $2; exit}' "$NEEDED")
    printf '      %-32s needed by %s\n' "$lib" "$who" >&2
done < "$MISSING"

n=$(wc -l < "$MISSING")
echo "    $n library name(s) unresolved." >&2
[ "$STRICT" = 1 ] && exit 1
exit 0
