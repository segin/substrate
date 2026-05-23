#!/bin/sh
# contrib/xorg-server/build.sh — WIP.
#
# See README.SUBSTRATE.md for the resurrection-of-Xfbdev plan; this
# stub exits non-zero so build orchestrators don't think the port
# succeeded.  Remove this and uncomment the real build when the
# kdrive/fbdev backend has been forward-ported into 1.20.14.

set -eu
echo "contrib/xorg-server: WIP — Xfbdev resurrection into 1.20.14 not yet done." >&2
echo "                     See contrib/xorg-server/README.SUBSTRATE.md." >&2
exit 1
