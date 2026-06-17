# Audio codec ports (substrate)

libogg, libvorbis, libopus, flac (libFLAC), faad2 (libfaad) — cross-built
for substrate via the shared helper `contrib/substrate-codec.sh` (faad2 is
CMake and self-contained).  Each: `./fetch.sh` then `./build.sh`, staging
into `dist-overlay/dist-<pkg>` and mirroring into the cross sysroot.
Build order: libogg first (libvorbis/flac depend on it); libopus + faad2
are standalone.  opus/flac force `-fstack-protector`, which substrate's
libc lacks the local helper for, so they pass `--disable-stack-protector`
/ `--disable-stack-smash-protection` (plus `-fno-stack-protector`).
