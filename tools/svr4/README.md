# System V Release 4 host tools

Host-side tools for the SVR4 personality (`PERS_SVR4`, `usr.man/man4/svr4.4`).
All run on the Linux build host, not on substrate.

**They need distribution media that is not in this repository.**  Each says
at the top what to obtain and how to pass it in.  None is part of any build
or test.

| tool | what it does |
| --- | --- |
| `build-image.sh` | build a populated `/perso/svr4` ext2 image from a set of UNIX System V/386 Release 4.0 floppy images |
| `s5fs.py` | read a System V `s5` filesystem image (list, cat, extract) -- the boot floppies |
| `svr4cpio.py` | list or extract the cpio archives on a Foundation Set floppy or a package datastream |

## The media

Written against Intel's UNIX System V/386 Release 4.0 Version 2 (1990), 58
5.25" floppies, as archived by WinWorld.  Three kinds of volume:

- **s5 filesystems**: the boot floppies.  Superblock at byte 512 (magic
  `0xfd187e20` at +0x1f8, block size from `s_type`), 64-byte inodes from
  block 2 with thirteen 3-byte little-endian block numbers, 16-byte
  directory records.  The first boot floppy ships once per disk controller
  and has its filesystem at byte 15360, after the boot code; `s5fs.py`
  finds it by the magic.  These are the only place the release ships
  `/sbin/sh`, `/sbin/init` and `/usr/lib/libc.so.1`.
- **Foundation Set**: a volume label, then one `newc` cpio archive per
  floppy starting at byte 15360.  No file spans floppies.
- **package datastreams** (`# PaCkAgE DaTaStReAm`): a 512-byte header and
  then cpio archives on 512-byte boundaries.  Files are under `reloc/` or
  `root/`; a package's later floppies have no header and use `reloc.N/`
  and `root.N/`.

Most files ship `compress`ed under their final names and are expanded.

## What the image is and is not

It is the files, with the links an installation makes (the `ln` commands in
the boot floppy's install scripts, and the packages' `pkgmap` link
entries), made relative so they stay inside `/perso/svr4`.  It is not an
installed system: no install script is run, so anything a `postinstall`
would have created is absent (`/usr/bin/awk`, for one), and devices and
ownership are not applied.

    7z x "Intel Unix System V R4.0 V2.0 (1990) (5.25-1.2mb).7z"
    tools/svr4/build-image.sh \
        -m "Intel Unix System V R4.0 V2.0 (1990) (5.25-1.2mb)" -o svr4.img

About 4900 files in a 256 MB image.  `--base-only` leaves the packages out.

## Dell UNIX System V Release 4

`build-image.sh` also takes the factory tape of Dell UNIX SVR4 Issue 2.2
(1992), as archived by Tenox (`tenox.pdp-11.net/os/dellunix/`,
`DellSVR4v22.tar.lz`), which is the same kinds of volume in tape-sized
files rather than on floppies:

- `file1`: one `newc` cpio archive of the whole installed root, starting at
  byte 0 -- a fourth kind, a *base system archive*, told from a package's
  later floppy by its first member not being a package's.  A file with
  several links has its data on the last of its names only; `svr4cpio.py`
  links the others to it.
- `file2`, `file5`: package datastreams holding several packages each (TeX,
  emacs, the GNU tools, mail, manual pages, X11; FrameMaker).  Each
  package's `pkgmap` is also in the stream under the package's name.
- `boot.img`, `system.img`, `file3`: s5 filesystems, the installation's own
  tools; laid down first and replaced by the base system.
- `file4`: `compress`ed cpio of source code; not a root, skipped.

      bsdtar xf DellSVR4v22.tar.lz
      tools/svr4/build-image.sh -m DellSVR4/Factory -o dellunix.img -s 640 -l dellunix

About 15100 files in a 640 MB image.  The binaries are ELF with
`/usr/lib/libc.so.1` as interpreter and run under the SVR4 personality as
Intel's do.  FrameMaker is not installed: its files go to a directory the
package asks for (`$INSTALL`), and such paths are left out.
