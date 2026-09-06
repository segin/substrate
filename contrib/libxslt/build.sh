#!/bin/sh
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
SUBSTRATE_TOP="$(cd "${HERE}/../.." && pwd)"
: "${STAGE1_PREFIX:=/opt/substrate}"; : "${JOBS:=$(nproc 2>/dev/null || echo 4)}"
SR="${STAGE1_PREFIX}/i386-unknown-substrate"; PATH="${STAGE1_PREFIX}/bin:${PATH}"; export PATH
# config.sub/config.guess via the shared helper.  Copying them straight out
# of contrib/binutils/build/ only works when the toolchain was built THIS
# run: build.sh sets SKIP_TOOLCHAIN=1 on a CI toolchain-cache hit, and the
# cache does not carry that extracted tree, so the old `ls -d ... | head -1`
# left BINU empty and the port died on `cp -f /config.sub`.  The helper
# prefers binutils when it is there, patches the tree's own copy when it is
# not, and asserts the result actually accepts the triple.
. "${HERE}/../substrate-autotools.sh"
cfgsub() { substrate_config_sub_fix "."
  sh "${SUBSTRATE_TOP}/contrib/substrate-libtool-shared.sh" ./configure >/dev/null 2>&1 || true; }
osabi_mirror() { # $1=DEST  (stamp .so + mirror libs/headers/pc to sysroot)
  find "$1" -name "*.la" -delete
  for so in $(find "$1/usr/lib" -name "*.so.*" -type f 2>/dev/null); do printf "\100" | dd of="$so" bs=1 seek=7 count=1 conv=notrunc status=none; done
  cp -a "$1"/usr/lib/*.so* "${SR}/lib/" 2>/dev/null || true
  cp -an "$1"/usr/include/* "${SR}/include/" 2>/dev/null || true
  cp -a "$1"/usr/lib/pkgconfig/*.pc "${SR}/lib/pkgconfig/" 2>/dev/null || true; }
TREE="${HERE}/build/libxslt-1.1.39"; DEST="${SUBSTRATE_TOP}/dist-overlay/dist-libxslt"
[ -d "${TREE}" ] || { echo "run ./fetch.sh first" >&2; exit 1; }
cd "${TREE}"; cfgsub
XC="${HERE}/build/xml2-config"
cat > "${XC}" <<XCFG
#!/bin/sh
case "\$1" in
  --version) echo "2.11.9";; --cflags) echo "-I${SR}/include/libxml2";;
  --libs) echo "-L${SR}/lib -lxml2 -lm -lpthread -ldl";; --prefix) echo "${SR}";; *) ;;
esac
XCFG
chmod +x "${XC}"
./configure --host=i386-unknown-substrate --prefix=/usr --enable-shared --enable-static \
    --without-python --without-crypto --without-debug --with-libxml-prefix="${SR}" XML_CONFIG="${XC}" \
    CC=i386-unknown-substrate-gcc CFLAGS="-march=i486 -mtune=i486 -O2 -g -fno-pie" \
    CPPFLAGS="-I${SR}/include/libxml2" LDFLAGS="-L${SR}/lib"
make -j"${JOBS}"; rm -rf "${DEST}"; make install DESTDIR="${DEST}"
osabi_mirror "${DEST}"
echo "==> libxslt staged under ${DEST}/usr"
