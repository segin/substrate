# libopus on Substrate

Cross-build of **libopus 1.5.2** (the Opus audio codec — RFC 6716).  Standalone:
the codec needs no libogg.  Pulled in as a codec dependency of PsyMP3, which
includes `<opus/opus.h>` and `<opus/opus_multistream.h>`.

`fetch.sh` pins the release tarball + SHA-256; `build.sh` cross-builds as a
linux host (CC stays the substrate cross gcc) so libtool emits the shared
library, stamps OSABI 0x40, and mirrors `libopus.*`, the `opus/` headers, and
`opus.pc` into the cross sysroot for the dependent ports.
