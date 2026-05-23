# encodings 1.1.0 — substrate port (data-only)

X font encoding tables (.enc files).  Upstream's configure requires
mkfontscale to generate encodings.dir; we sidestep that by staging
the .enc files directly and generating encodings.dir with sed in
build.sh.  Avoids the freetype/libpng/harfbuzz dep chain that
mkfontscale pulls in.
