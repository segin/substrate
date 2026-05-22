# e2tools on substrate

Upstream:  https://github.com/e2tools/e2tools
Pinned:    0.1.0
Tarball:   `https://github.com/e2tools/e2tools/releases/download/v0.1.0/e2tools-0.1.0.tar.gz`
SHA-256:   `c1a06b5ae2cbddb6f04d070e889b8bebf87015b8585889999452ce9846122edf`

## Why

`e2tools` is a small set of command-line utilities that read and
write files inside an ext2/3/4 filesystem **image** without mounting
it: copy files in/out, list directories, make directories, remove
and move files, tail a file.  Useful for image preparation and
inspection from a normal user context.

## Scope

- `/usr/bin/{e2cp,e2ls,e2mkdir,e2rm,e2ln,e2mv,e2tail,e2tools}`
- man pages → `/usr/share/man/man{1,7}/`

## Depends on

- `contrib/e2fsprogs` — `libext2fs` + `libcom_err` (static), their
  headers and `ext2fs.pc` / `com_err.pc` pkg-config files.

## Substrate patches

- `0001-config-sub-substrate.patch` — `config.sub` substrate OS.

## Build notes

- `-include alloca.h` — `util.c` calls `alloca()` without including
  `<alloca.h>`; substrate does not re-expose `alloca` through
  `<stdlib.h>` as glibc does.
- `LIBS=-lregex` — e2tools uses POSIX regex (`regcomp`/`regexec`/
  `regfree`); on substrate these are in the separate `libregex`
  (`usr.lib/regex`), not libc.

## Layout

    contrib/e2tools/
        README.SUBSTRATE.md
        fetch.sh
        build.sh
        series
        patches/
        build/    ← NOT vendored
