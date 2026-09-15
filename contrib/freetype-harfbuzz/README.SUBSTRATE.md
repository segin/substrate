# freetype-harfbuzz

The second FreeType pass.  Not a separate package: it reruns
`contrib/freetype/build.sh` with `FREETYPE_WITH_HARFBUZZ=1` and replaces
`dist-overlay/dist-freetype`, so the image carries a `libfreetype.so.6` that
links `libharfbuzz.so.0`.

FreeType and HarfBuzz depend on each other, the usual way distributions handle
it: `contrib/freetype` builds `--without-harfbuzz` early, `contrib/harfbuzz`
builds against it, and this port, listed right after harfbuzz, rebuilds
FreeType with it.  Ports built between the two passes link the first build;
the soname and symbols are the same.

- The HarfBuzz flags are given to configure directly rather than taken from
  pkg-config, because `harfbuzz.pc` requires `freetype2`, which would put the
  first pass's headers on FreeType's own include path.
- `build.sh` asserts the new `libfreetype.so.6` records `DT_NEEDED
  libharfbuzz.so.0`, strips the staging tree, and mirrors it into the cross
  sysroot itself, since `build.sh`'s per-port strip and sync look for a
  `dist-freetype-harfbuzz` tree that does not exist.
- The runtime `DT_NEEDED` cycle is safe: ld.so checks for an already-loaded
  object by SONAME before loading one.
