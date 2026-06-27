# libvorbis on Substrate

Cross-build of **libvorbis 1.3.7** (the Xiph Vorbis audio codec — a codec
dependency of PsyMP3).  `fetch.sh` pins the release tarball + SHA-256;
`build.sh` cross-builds as a linux host (CC stays the substrate cross gcc) so
libtool emits the shared libraries, stamps OSABI 0x40, and mirrors the result
(`libvorbis`, `libvorbisenc`, `libvorbisfile`, `include/vorbis`, and the `.pc`
files) into the cross sysroot for the dependent ports.

Depends on **libogg** (`contrib/libogg/`), which must already be staged in the
cross sysroot — `build.sh` points configure at it via `PKG_CONFIG_LIBDIR`,
`CPPFLAGS`/`LDFLAGS`, and `--with-ogg`.
