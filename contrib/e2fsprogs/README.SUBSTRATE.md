# e2fsprogs on substrate

Upstream:  https://e2fsprogs.sourceforge.net/  /  git.kernel.org tytso/e2fsprogs
Pinned:    1.47.2
Tarball:   `https://cdn.kernel.org/pub/linux/kernel/people/tytso/e2fsprogs/v1.47.2/e2fsprogs-1.47.2.tar.xz`
SHA-256:   `08242e64ca0e8194d9c1caad49762b19209a06318199b63ce74ae4ef2d74e63c`

## Why

`e2fsprogs` is the ext2/3/4 userspace toolset — `mke2fs`, `e2fsck`,
`tune2fs`, `dumpe2fs`, `debugfs`, `resize2fs`, `badblocks`, … — and
the libraries (`libext2fs`, `libcom_err`, `libe2p`, …) that
manipulate ext filesystem images.  Substrate has an in-kernel ext2
driver; e2fsprogs supplies the userspace creation and repair tools.
It is also the dependency that `contrib/e2tools` links.

## Scope

- Tools  → `/usr/sbin/` (mke2fs, e2fsck, debugfs, resize2fs, …)
- Static libs → `/usr/lib/lib{ext2fs,com_err,e2p,ss,uuid,blkid}.a`
- Headers → `/usr/include/{ext2fs,et,e2p,ss,uuid,blkid}/`
- pkg-config → `/usr/lib/pkgconfig/{ext2fs,com_err,e2p,ss,uuid,blkid}.pc`

Static libraries only: e2fsprogs builds shared objects through its
own hand-rolled ELF rules, which have no `host_os` case for
substrate.  The static archives are what `contrib/e2tools` links.

## Substrate patches

- `0001-config-sub-substrate.patch` — `config.sub` substrate OS.
- `0002-jfs-compat-tid-t-substrate.patch` — substrate's `<sys/types.h>`
  defines `tid_t` (a thread id); e2fsprogs's journal layer defines
  its own `tid_t`.  Skip e2fsprogs's typedef on substrate.
- `0003-no-sbrk-substrate.patch` — substrate has no `sbrk(2)`;
  e2fsprogs's resource tracker calls `sbrk(0)`.  Replaced with a
  null pointer (the diagnostic "Memory used" figure reads 0).

## Build notes

- `RDYNAMIC=` overrides configure's unconditional `-rdynamic`, which
  the substrate cross-gcc link spec does not implement.
- `ac_cv_func_mbstowcs=no` gates off `badblocks.c`'s wide-char
  progress-width refinement, which needs `wcswidth(3)` (substrate's
  libc has `mbstowcs` but not `wcswidth`).  The snprintf byte length
  is used instead — correct for substrate's C locale.
- `CFLAGS` force-includes `<sys/time.h>` (`struct timeval` is not
  transitively visible under substrate's strict headers).

## Layout

    contrib/e2fsprogs/
        README.SUBSTRATE.md
        fetch.sh
        build.sh
        series
        patches/
        build/    ← NOT vendored
