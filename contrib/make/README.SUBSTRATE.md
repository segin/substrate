# GNU make on substrate

Upstream:  https://www.gnu.org/software/make/
Pinned:    4.4.1   (released 2023-02-27)
Tarball:   `https://ftp.gnu.org/gnu/make/make-4.4.1.tar.gz`
SHA-256:   `dd16fb1d67bfab79a72f5e8390735c49e3e8e70b4945a15ab1f81ddb78658fb3`

## Why

Substrate currently has an in-tree `bin/make`.  GNU make 4.4.x is
what every real package's `./configure` expects — POSIX 2024 plus
the well-known GNU extensions: pattern rules, `$(eval)`, `$(call)`,
`$(shell)`, `MAKEFLAGS` propagation, parallel job control with
`-jN`, `.SECONDARY` / `.PHONY` / `.SECONDEXPANSION`, conditional
directives, `vpath`/`VPATH`.  Once on-image, third-party packages
become buildable on substrate itself (the bootstrap loop closes).

## Scope

- `make` cross-built for substrate, installed at `/usr/bin/make`.
- Symlink `/usr/bin/gmake` → `make` for portability with scripts
  that name GNU make explicitly.

## Layout

    contrib/make/
        README.SUBSTRATE.md
        fetch.sh
        build.sh
        series
        patches/
        build/    ← NOT vendored
