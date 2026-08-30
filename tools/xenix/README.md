# Xenix host tools

Host-side tools for the SCO Xenix/286 personality (`PERS_SCO_X286`). Both run
on the Linux build host, not on substrate.

| tool | what it does |
| --- | --- |
| `xenixfs.py` | read SCO Xenix / System V filesystem images (list, cat, extract), with MBR partition support |
| `build-image.sh` | build the populated `/perso/xenix286s` ext2 image from distribution media |

## Why `xenixfs.py` exists

Xenix distribution media is mostly `tar`, but **not all of it**, and the part
that isn't is the part that matters. `rts/n1.img` — the install floppy — is a
bootable Xenix filesystem, and it is the *only* place these ship:

    sh  rsh  cp  ln  mv  cpio  dd  fsck  mkdir  ps  tar

The tar volumes (`b1 b2 x1..x4 ga n2 n3`) contain 1047 files and not one of
those. Without a reader for the filesystem there is no Bourne shell, which is
what Word 3.0 execs when it wants a subprocess.

Linux cannot mount this: `sysvfs` dropped the Xenix variant, and the on-disk
details differ from stock System V anyway.

### On-disk format

Verified against SCO Xenix 2.3.2 media. Block size 1024.

    block 0        boot block
    block 1        superblock
    block 2..      inode list
    block s_isize  first data block

`s_isize` is the **first data block**, not the length of the i-list as stock
V7 documents it — on this media `s_isize == 12` and the root directory is at
block 12, not 14. Reading inodes never depends on this; the i-list always
starts at block 2.

Inode: 64 bytes, inode *N* at `2*1024 + (N-1)*64`, root is inode 2.

    +0   u16  mode        +8   u32  size
    +2   i16  nlink       +12  13 x 3-byte block numbers
    +4   u16  uid         +51  atime, mtime, ctime (u32 each)
    +6   u16  gid

The 3-byte block numbers are plain **little-endian 24-bit**, not the PDP-11
`l3tol` middle-endian packing V7 used. 10 direct, then single/double/triple
indirect; indirect blocks hold u32 little-endian block numbers.

Directories are arrays of 16-byte records: `u16` inode, 14-byte NUL-padded
name. Inode 0 means a free slot.

### Usage

    xenixfs.py partitions IMAGE
    xenixfs.py info       IMAGE [-p N | -o BYTES]
    xenixfs.py ls         IMAGE [PATH] [-R]
    xenixfs.py cat        IMAGE PATH
    xenixfs.py extract    IMAGE DEST [--path PATH]

With no `-p`/`-o`, a partitioned image uses its sole Xenix partition (MBR
types `0x02` XENIX root, `0x03` XENIX usr) and an unpartitioned one is read
from offset 0 — so floppy images just work. Two Xenix partitions is an error
telling you to pick one.

`extract` reproduces hard links rather than duplicating data (`sh` and `rsh`
are one inode, as are `cp`/`ln`/`mv`), preserves modes including setuid and
sticky, restores mtimes, and skips device nodes with a count.

    $ xenixfs.py ls ~/Downloads/286/rts/n1.img /bin
    -rwx--x--x   3    13     9476  cp
    -rwx--x--t   2    15    38887  sh
    ...

## `build-image.sh`

Builds the ext2 image substrate mounts at `/perso/xenix286s`. The image is
ext2 on purpose: substrate reads it through its own VFS and runs the x.out
binaries under `PERS_SCO_X286`, so the container is substrate's business and
only the file contents are Xenix's.

    ./build-image.sh [-m MEDIA_DIR] [-o OUTPUT] [-s SIZE_MB] [--minimal]

Defaults: media `~/Downloads/286`, output `<repo>/xenix286s.img`, 64 MB, and
**every product on the media**. `--minimal` is the runtime system plus Word.

It stages the tar volumes, pulls `/bin` out of `n1.img` with `xenixfs.py`,
adds Word 3.0 from `msw/word.img`, runs `bin/xenix/fix-termdesc-ansi.sh` over
the staged `termdesc`, then installs the Development System, manual pages,
text processing, CGI, Lyrix, COBOL, FoxBASE, BASIC, Multiplan, the public
domain supplement, PET, Demos Commander and the precompiled utilities.

### Three packaging shapes

Only the first can be untarred blindly:

1. **`custom` format** — real files under `./bin ./etc ./lib ./usr`, plus
   `./tmp` holding the installer's scaffolding (`_lbl` volume labels, `perms`
   manifests, `fixperm`, `brand`, `install`, `init.*` hooks, and payloads it
   would place itself). Everything but `./tmp` is taken. **The rts volumes
   carry this too** — miss it and the image gets a /tmp full of `init.rts`,
   `cmds.oa` and friends.
2. **msinstall format** (BASIC) — members named `../../usr/...`, meant to be
   untarred from a directory two levels down. `tar` refuses `..` without
   `-P`; `--strip-components=2` then lands them correctly at `usr/...`.
3. **no paths at all** (Multiplan) — bare `MP.HLP`, `mp`, `mp.exec`,
   `termcap`. The `mp` wrapper execs `/usr/lib/MSTOOLS/mp.exec`, which is
   where the payload goes; `mp` itself is `/usr/bin/mp`.

Demos Commander is its own shape again: a `dist286/` tree whose `INSTALL`
targets `/usr/bin/deco` and `/usr/lib/deco`.

Two wrinkles it handles:

- **Execute-only files.** Xenix ships ~20 uucp binaries mode `0111`/`0100`,
  which the build user cannot read, so `mke2fs -d` fails on them. They are
  staged `u+r`, then their real modes are restored inside the image with
  `debugfs`.
- **Ownership.** `fakeroot` makes the staged tree root-owned so the image
  does not inherit the build user's uid.

The result is ~2375 files: 153 entries in `/bin`, 196 in `/usr/bin`, 11 man
sections.

## What actually runs

A 43-command sample under `PERS_SCO_X286`: **29 run, 8 crash, 6 will not
load.** The two failure classes are distinct and neither is an image problem.

**Will not load** — `exec: no handler matched`. These are **8086** binaries
(`x_cpu` 0x44) and the personality is 286-only (`x_cpu` 0x49). The census of
the media is 207 i286, 97 8086, 15 `0x69`, 1 i386, and the entire Development
System is 8086 — which is why `cc`, `masm`, `ld` and `bc` do not run. SCO
shipped it as the *x86* Development System; the readme says so. Running it
needs the SCO-X/86 personality (id 133), which is a slot, not an
implementation.

**Crash** — SIGSEGV early in text: `units`, `factor`, `uptime`, `tput`,
`awk`, `emacs`, `kermit`, `adb`. These are i286 with the same `x_renv`
(0xc80f) and the same two-segment layout as `date`, which works, so this is a
personality gap rather than anything about the file. Not diagnosed.

Working, among others: `date echo pwd ls cat wc sum uname id basename dirname
expr cal od hd line env printenv sh who tty sed grep clear random deco`, and
Word 3.0.

## Media layout

`~/Downloads/286` (not in the repo — distribution images):

    rts/   Xenix 2.3.2 runtime: n1 (install floppy, a filesystem),
           b1 b2 (base), x1-x4 (extended), ga, n2 n3 (headers)
    msw/   Microsoft Word 3.0          msp/  Multiplan 2.0
    dev/   Development System 2.2.1a   msb/  BASIC 5.41
    man/   manual pages                txt/  text processing
    lrx/   Lyrix 5.0.4a                fox/  FoxBASE
    vsc/   VS Cobol                    cgi/  CGI 1.1.0d
    msc/   pds (public domain supplement), utils286, deco, games

Only `rts/` and `msw/` are installed by `build-image.sh`; the rest are
available to add.
