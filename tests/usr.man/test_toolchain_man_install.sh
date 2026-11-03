#!/bin/sh
# Check that the binutils-style tools and libdemangle leave their manual
# pages where mandoc looks for them.
#
# Every manual page is kept under usr.man/ and installed by usr.man/Makefile
# to $(DESTDIR)/usr/share/man/, the tree mandoc searches by default.  This
# asserts that
#   - no tool's own "make install" writes a page under usr/man/, where
#     mandoc never finds it, and
#   - usr.man's "make install" puts each page under usr/share/man/.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)

pass() { printf 'ok: %s\n' "$1"; }
fail() { printf 'FAIL: %s\n' "$1" >&2; failed=1; }

failed=0
SCRATCH=$(mktemp -d "${TMPDIR:-/tmp}/man-install.XXXXXX")
trap 'rm -rf -- "$SCRATCH"' EXIT INT TERM
DEST="$SCRATCH/dest"
mkdir -p "$DEST"

# The tools' install targets are dry-run (make -n): what matters is where
# their recipes put files, and running them for real would first link the
# programs.
for dir in usr.lib/demangle usr.bin/nm usr.bin/ar usr.bin/addr2line \
    usr.bin/cxxfilt; do
	if ! make -n -C "$ROOT/$dir" NATIVE_BUILD=1 DESTDIR="$DEST" install \
	    >"$SCRATCH/make.log" 2>&1; then
		cat "$SCRATCH/make.log" >&2
		fail "$dir: make -n install"
	elif grep -F "$DEST/usr/man/" "$SCRATCH/make.log" >&2; then
		fail "$dir: make install writes under usr/man"
	else
		pass "$dir: make install writes nothing under usr/man"
	fi
done

if make -C "$ROOT/usr.man" DESTDIR="$DEST" install \
    >"$SCRATCH/make.log" 2>&1; then
	pass "usr.man: make install"
else
	cat "$SCRATCH/make.log" >&2
	fail "usr.man: make install"
fi

for page in man1/nm.1 man1/ar.1 man1/ranlib.1 man1/addr2line.1 \
    man1/c++filt.1 man3/demangle.3 man3/demangle_buf.3 \
    man3/demangle_free.3 man3/demangle_version.3; do
	if [ -f "$DEST/usr/share/man/$page" ]; then
		pass "usr/share/man/$page installed"
	else
		fail "usr/share/man/$page not installed"
	fi
done

[ "$failed" -eq 0 ]
