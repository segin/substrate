#!/bin/sh
#
# contrib/mandoc/build.sh — configure + build + install mandoc for
# substrate.  Produces:
#   /usr/bin/{mandoc,man,soelim,demandoc}
#   /usr/lib/libmandoc.a (used by other contrib ports — qman)
#   /usr/include/mandoc.h, mandoc_aux.h, mdoc.h, man.h, roff.h
#   /usr/share/man/man1/{mandoc,man,soelim,demandoc}.1
#   /usr/share/man/man3/mandoc.3
#   /usr/share/man/man5/{mdoc,man}.5
#   /usr/share/man/man7/{mdoc,man}.7
#
# mandoc uses a hand-rolled POSIX shell configure with a
# configure.local override.  We drive it via env vars.

set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"
VERSION="1.14.6"
TREE_DIR="${HERE}/build/mandoc-${VERSION}"

if [ -z "${SUBSTRATE_TOP:-}" ]; then
    p="${HERE}"
    while [ "${p}" != "/" ] && [ ! -f "${p}/AGENTS.md" ] && [ ! -f "${p}/CLAUDE.md" ]; do
        p=$(dirname "${p}")
    done
    SUBSTRATE_TOP="${p}"
fi
: "${STAGE1_PREFIX:=/opt/substrate}"
: "${DESTDIR:=${SUBSTRATE_TOP}/dist-overlay/dist-mandoc}"
: "${JOBS:=$(nproc 2>/dev/null || echo 4)}"

PATH="${STAGE1_PREFIX}/bin:${PATH}"
export PATH

[ -d "${TREE_DIR}" ] || { echo "build.sh: run ./fetch.sh first" >&2; exit 1; }

cd "${TREE_DIR}"

# mandoc's configure honors a configure.local file for per-host
# overrides.  Drop ours next to configure so it gets picked up.
# Substrate is cross-compiling, so configure's "compile + run a
# probe binary" path can't actually execute — pre-set every HAVE_*
# the substrate libc supplies so the runtime tests get skipped.
cat > configure.local <<'EOF'
CC=i386-unknown-substrate-gcc
AR=i386-unknown-substrate-ar
RANLIB=i386-unknown-substrate-ranlib
CFLAGS="-march=i486 -mtune=i486 -O2 -g -fno-pie"
LDFLAGS="-fno-pie -Wl,--copy-dt-needed-entries"
# LDADD comes AFTER objects on the link line — that's where we
# put the libraries the .o files actually reference (regex, etc.).
LDADD="-lregex"

PREFIX=/usr
BINDIR=/usr/bin
SBINDIR=/usr/sbin
INCLUDEDIR=/usr/include
LIBDIR=/usr/lib
MANDIR=/usr/share/man
EXAMPLEDIR=/usr/share/examples/mandoc

BINM_MAN=man
BINM_APROPOS=apropos
BINM_WHATIS=whatis
BINM_MAKEWHATIS=makewhatis
BINM_SOELIM=soelim
# substrate has /bin/more but no less yet — make `man` default to
# more.  -T is a less(1)-ism; HAVE_LESS_T=0 disables it.
BINM_PAGER=more
HAVE_LESS_T=0

# Cross-compile probe overrides — substrate has these.
HAVE_ATTRIBUTE=1
HAVE_DIRENT_NAMLEN=0
HAVE_ENDIAN=1
HAVE_ERR=1
HAVE_GETLINE=1
HAVE_ISBLANK=1
HAVE_LESS_T=1
HAVE_MKDTEMP=1
HAVE_MKSTEMPS=1
HAVE_NANOSLEEP=1
HAVE_NTOHL=1
HAVE_O_DIRECTORY=1
HAVE_PATH_MAX=1
HAVE_PROGNAME=0
HAVE_REALLOCARRAY=1
HAVE_RECALLOCARRAY=0
HAVE_STRCASESTR=1
HAVE_STRLCAT=1
HAVE_STRLCPY=1
HAVE_STRNDUP=1
HAVE_STRPTIME=1
HAVE_STRSEP=1
HAVE_STRTONUM=0
HAVE_VASPRINTF=1
# Substrate's libc doesn't ship <langinfo.h> / nl_langinfo yet,
# and mandoc's wide-char path uses it.  Force ASCII output until
# substrate grows langinfo support — the man pages render fine
# in ASCII for our purposes.
HAVE_WCHAR=0
HAVE_SYS_ENDIAN=0
HAVE_REWB_BSD=0
HAVE_REWB_SYSV=1
HAVE_GETSUBOPT=0
HAVE_FTS=0
HAVE_FTS_COMPARE_CONST=0
HAVE_STRINGLIST=0

# Substrate-side absent:
HAVE_PLEDGE=0
HAVE_SANDBOX_INIT=0
HAVE_CMSG=0
HAVE_RECVMSG=0
HAVE_EFTYPE=0

# No libdb / ohash backend yet — apropos/whatis walk MANPATH.
HAVE_OHASH=0
EOF

echo "==> configure"
./configure

echo "==> make -j${JOBS}"
make -j"${JOBS}"

echo "==> install into ${DESTDIR}"
rm -rf "${DESTDIR}"
mkdir -p "${DESTDIR}"
make install DESTDIR="${DESTDIR}"

echo "==> Done.  /usr/bin/{mandoc,man,soelim,demandoc} +"
echo "    libmandoc.a + headers + man pages staged under ${DESTDIR}"
