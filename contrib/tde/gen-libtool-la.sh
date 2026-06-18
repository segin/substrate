#!/bin/sh
# gen-libtool-la.sh — generate libtool ".la" stubs for TDE plugins.
#
# TDE's KLibLoader::findLibrary() resolves a plugin given by bare name
# (e.g. "twin3_default", "kded_kcookiejar") by appending ".la" and looking
# the libtool archive up under the "module"/"lib" resource dirs
# (tdecore/klibloader.cpp:makeLibName).  A CMake-built TDE installs only the
# ".so" — with no ".la", findLibrary() returns empty and EVERY plugin fails
# to load: twin finds no window-decoration plugin and exits, kded loads no
# kded_* modules, kcontrol finds no kcm_* modules, etc.
#
# A real (autotools/libtool) TDE ships a ".la" next to each plugin.  This
# script regenerates those minimal stubs for a staged tree so KLibLoader
# can resolve plugins by name.
#
# Usage: gen-libtool-la.sh <staged-root> [<staged-root> ...]
#   where <staged-root> is a dist tree containing opt/trinity/lib[/trinity].
#
# A plugin is an UNVERSIONED .so (foo.so), as loaded by KLibLoader; versioned
# shared libraries (libfoo.so.14.0.0) are linked by ld.so via DT_NEEDED and
# need no .la.

set -eu

gen_one() {
    so="$1"                       # absolute path to the plugin .so
    dir=$(dirname "$so")
    base=$(basename "$so" .so)
    la="$dir/$base.la"
    # strip the staged-root prefix to recover the on-target libdir
    libdir=$(printf '%s' "$dir" | sed 's#.*\(/opt/trinity/lib.*\)#\1#')
    cat > "$la" <<EOF
# $base.la - a libtool library file
# Generated for substrate by contrib/tde/gen-libtool-la.sh (KLibLoader needs .la)
dlname='$base.so'
library_names='$base.so $base.so $base.so'
old_library=''
dependency_libs=''
current=0
age=0
revision=0
installed=yes
shouldnotlink=no
dlopen=''
dlpreopen=''
libdir='$libdir'
EOF
}

count=0
for root in "$@"; do
    [ -d "$root" ] || continue
    # plugins live under lib/ and lib/trinity/ (and lib/trinity/plugins/...)
    for d in "$root"/opt/trinity/lib "$root"/opt/trinity/lib/trinity; do
        [ -d "$d" ] || continue
    done
    # find every unversioned *.so anywhere under opt/trinity/lib
    find "$root/opt/trinity/lib" -type f -name '*.so' 2>/dev/null | while read -r so; do
        gen_one "$so"
    done
    n=$(find "$root/opt/trinity/lib" -type f -name '*.la' 2>/dev/null | wc -l)
    echo "  $root: $n .la stubs"
    count=$((count + 1))
done
echo "gen-libtool-la.sh: processed $count tree(s)"
