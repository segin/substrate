# Venix host tools

Host-side tools for the Venix/86 personality (`PERS_VENIX`, rooted at
`/perso/venix`, `venix(4)`).  Both run on the Linux build host, not on
substrate.

**They need floppy images that are not in this repository.**  Each says at
the top what to obtain and how to pass it in.  Neither is part of any build
or test.

| tool | what it does |
| --- | --- |
| `build-image.sh` | build a populated `/perso/venix` ext2 image from the floppies of a Venix/86 2.1 system |
| `venixtar.py` | read those floppies: list, extract, or turn one into the tar file it is |

    7z x 'Venix-86 2.1 (1985) (5.25-1.2mb).7z'
    tools/venix/build-image.sh -m 'Venix-86 2.1 (1985) (5.25-1.2mb)' -o venix86.img

Attach the image as a second disk and mount it at `/perso/venix`:

    mkdir -p /perso/venix
    mount /dev/storage/virtio0 /perso/venix ext2
    /perso/venix/bin/sh

## The media

VenturCom Venix/86 2.1 (1985), as archived by WinWorld ("Venix-86 2.1
(1985) (5.25-1.2mb)"): nine 1.2 MB floppies `BACKUP1.IMG` ... `BACKUP9.IMG`
and a 360K `XFER.IMG`.

It is not installation media.  The nine are a `tar` backup of a system that
was in use: Venix/86 itself -- `/bin`, `/usr/bin`, `/etc`, `/lib` with the C
compiler and `libc.a`, `/usr/include`, the kernel `/venix` -- and its
owner's files.  `XFER.IMG` is a boot floppy with a small V7 filesystem and
is not read.

Each floppy is a Version 7 tar archive by itself; no file continues onto
the next.  An image of one does not read as tar, because Venix used the
disk in a different order from the one an image file is in: side 0 from
cylinder 0 out to 79, then side 1 from cylinder 79 back to 0.
`venixtar.py` has the arithmetic, and `venixtar.py raw` writes a floppy out
in archive order for any other tar to read.

Everything the personality was written from is in the image: `/lib/libc.a`
holds the system-call stubs (a dozen instructions each), and
`/usr/include/a.out.h`, `sgtty.h` and `sys/stat.h` the formats.
