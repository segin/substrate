# harfbuzz (14.6.0)

Text shaping engine — a hard dependency of Pango 1.42 and PsyMP3.  C++
(built without libstdc++), cross-built with Meson via substrate_sysroot
against glib2/freetype; cairo and ICU backends, gobject/introspection, the
raster/vector/GPU libraries, tests, docs and utilities disabled.  Produces
libharfbuzz.so and libharfbuzz-subset.so + their .pc files.

HarfBuzz has built with Meson only since 8.x.  `build.sh` writes a Meson
cross file for the substrate toolchain.  Meson's `threads` dependency
passes `-pthread`, which substrate's gcc driver does not accept, so the
compilers named in the cross file are small wrappers (written by
`build.sh`) that pass `-D_REENTRANT -lpthread` in its place, as the cairo
and lmdb ports do by hand.

The 64-bit build can be OOM-killed with many parallel jobs (its larger
translation units at `-O2 -g`); `JOBS=3 contrib/port64.sh harfbuzz` built.
