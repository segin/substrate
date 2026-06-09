# font-bh-lucida (X.Org B&H Lucida bitmap fonts)

Provides `-b&h-lucida*` / `-b&h-lucidatypewriter*` (75dpi + 100dpi).
CDE's dtcm requests these as a FontSet; without them the FontSet build
fails and drawing the broken set **segfaults Xfbdev**, which dies holding
the evdev keyboard/mouse grab → the whole desktop freezes (cursor still
moves, clicks/keys dead).  Source-only patch series; `fetch.sh` downloads
+ SHA-verifies the four upstream tarballs, `build.sh` stages the BDFs
(deriving ISO8859-1 variants, same as font-adobe).  The merged per-dir
`fonts.dir` and the CDE `-dt-*` `fonts.alias` are installed by
`build-rootfs.sh`'s `finalize_x_fonts` step (adobe + bh share 100dpi/75dpi).
