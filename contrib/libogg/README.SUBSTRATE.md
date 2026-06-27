# libogg on Substrate

Cross-build of **libogg 1.3.5** (the Ogg bitstream container, foundation for
Vorbis / Opus / Speex).  `fetch.sh` pins the release tarball + SHA-256;
`build.sh` cross-builds as a linux host (CC stays the substrate cross gcc) so
libtool emits the shared library, stamps OSABI 0x40, and mirrors the result
into the cross sysroot for the dependent codec ports.
