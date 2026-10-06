# System V Release 3 host tools

Host-side tools for the SVR3 personality (`PERS_SVR3`).  All run on the
Linux build host, not on substrate.

**They need distribution media that is not in this repository.**  Each says
at the top what to obtain and how to pass it in.  None is part of any build
or test.

| tool | what it does |
| --- | --- |
| `build-image.sh` | build a populated `/perso/svr3` ext2 image from AT&T UNIX System V/386 Release 3 floppies and tapes |
| `imd2raw.py` | turn an ImageDisk (`.IMD`) floppy image into a raw sector image |

`build-image.sh` also uses `../svr4/s5fs.py` for the boot floppies, which
are the same `s5` filesystems Release 4's are.

## The media

Written against the archives bitsavers keeps under `bits/ATT/SYSV_386/`:

| archive | what it gives |
| --- | --- |
| `SYSV_386_3.2.3_cartridge_16user.zip` | Release 3.2.3: boot floppies and the cartridge tape (base system and packages) |
| `SYSV_386_3.2.3_1.44mb_2user.zip` | the same release on floppies |
| `SYSV_386_3.2_SDS_4.1.5.zip` | the C Software Development Set (`cc`, `as`, `ld`, ...) and ETI |
| `SYSV_386_3.1_1.2mb_disk1_missing.zip` | Release 3.1 without its first floppy, so without `/bin/sh` and `/shlib` |

Floppies are ImageDisk files.  After a one-cylinder label each holds
either an `s5` filesystem (the boot floppy: the only source of `/bin/sh`
and `/shlib/libc_s`) or an old-format ASCII cpio archive (magic `070707`).
A package is one archive continued across as many floppies as it needs, so
a floppy whose data does not start with a header belongs to the one before
it.  The tape is the same archives, one per tape file.

A package carries `Name`, `Files`, `Install` and `Remove` for
`installpkg`.  The `Install` script is not run: files are put where the
`Files` list says, which is all most of the scripts do.

    unzip SYSV_386_3.2.3_cartridge_16user.zip
    unzip SYSV_386_3.2_SDS_4.1.5.zip
    tools/svr3/build-image.sh -m SYSV_386_3.2.3_cartridge_16user \
        -m SYSV_386_3.2_SDS_4.1.5 -o svr3-323.img

The binaries are i386 COFF.  The system ones are linked against the static
shared library `/shlib/libc_s`; `/bin/sh` is not, and makes its system
calls itself with `lcall $7,$0`.
