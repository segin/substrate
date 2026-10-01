# Go fonts 2.010 — substrate port

The Go font family by Bigelow & Holmes: Go Regular, Medium and Bold (each
with an italic), Go Smallcaps (and italic), and Go Mono (regular, bold,
italic, bold italic) -- 12 TrueType fonts.  Licence: the Go project's
BSD-style licence, plus its patent grant (`/usr/share/doc/font-go/`).

## Build

```sh
./fetch.sh        # golang.org/x/image v0.46.0 module zip from proxy.golang.org
./build.sh        # -> dist-font-go/usr/share/fonts/go/*.ttf
```

No compile step and no dependencies.

## Source and verification

The fonts live in the `golang.org/x/image` module (`font/gofont/ttfs/`).
They are fetched as the module zip from the Go module proxy, whose zips are
immutable, rather than as a GitHub archive, which GitHub generates on the
fly with no guarantee of identical bytes.  `fetch.sh` pins the zip's
SHA-256, and the zip's contents were checked against the public Go checksum
database: they hash to the `h1:` value `sum.golang.org` records for
`golang.org/x/image v0.46.0`.  The fonts have not changed since 2.010, so a
later module version would carry the same files.
