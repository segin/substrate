# speex on Substrate

Cross-build of **speex 1.2.1** (the Xiph Speex speech codec — a codec
dependency of PsyMP3, which probes for it via
`PKG_CHECK_MODULES([SPEEX],[speex >= 1.2])`).  `fetch.sh` pins the release
tarball + SHA-256; `build.sh` cross-builds as a linux host (CC stays the
substrate cross gcc) so libtool emits the shared library, stamps OSABI 0x40,
and mirrors the result (`libspeex`, `include/speex`, and `speex.pc`) into the
cross sysroot for the dependent ports.

The example binaries (`speexenc` / `speexdec`) are disabled
(`--disable-binaries`); only those link **libogg** (`contrib/libogg/`).
`libspeex` itself carries no Ogg dependency.  Substrate has an FPU, so the
default floating-point API is kept (no `--enable-fixed-point` /
`--disable-float-api`); SSE is off by default.  `-fno-stack-protector` is
required because substrate only defines `__stack_chk_fail_local` in crt0.
