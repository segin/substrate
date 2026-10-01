# GNU Unifont 18.0.01 — substrate port

A glyph for every assigned code point in Unicode's Basic Multilingual Plane
(`unifont.otf`) and much of the planes above it (`unifont_upper.otf`),
drawn as 8x16 / 16x16 pixel glyphs.  It is the last-resort fallback: when
no other installed font has a character -- CJK, Hangul, Thai, Devanagari,
rare symbols -- fontconfig uses Unifont instead of drawing an empty box.
Licence: GPLv2+ with the GNU font embedding exception, or SIL OFL 1.1
(both texts under `/usr/share/doc/font-unifont/`).

## Build

```sh
./fetch.sh        # GNU release tarball; SHA-256 matches nixpkgs' unifont
./build.sh        # -> dist-font-unifont/usr/share/fonts/unifont/*.otf
```

No compile step: the release tarball carries the OpenType files ready-built
in `font/precompiled/`.

## Notes

- Only the two general-purpose fonts are installed, without the version in
  the file name so an update replaces them.  Skipped: `unifont_jp`
  (Japanese glyph forms), `unifont_csur` (ConScript private-use),
  `unifont_t` (Tolkien scripts), the sample fonts and the BDF/PCF/PSF/hex
  sources.
- fontconfig's stock `69-unifont.conf`, already enabled, ranks Unifont
  after the real families, so it fills gaps without replacing them.
  Verified: `fc-match :charset=4e00` (CJK) and `:charset=e01` (Thai) both
  resolve to Unifont, and Tk renders Chinese, Japanese, Korean, Thai and
  Devanagari through it.
