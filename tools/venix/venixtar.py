#!/usr/bin/env python3
#
# venixtar.py - read the floppies of a Venix/86 2.1 backup set.
#
# REQUIRES EXTERNAL MEDIA.  This reads floppy images of VenturCom Venix/86,
# which are not in the repository; see README.md for what to obtain.
# Nothing in any build or test runs it.
#
#   venixtar.py list    IMAGE...            what is on the floppies
#   venixtar.py extract DIR IMAGE...        unpack them under DIR
#   venixtar.py raw     IMAGE OUTPUT        one floppy as the tar file it is
#
# THE FORMAT
#
# Each 1.2 MB floppy (80 cylinders, 2 sides, 15 sectors of 512 bytes) is a
# Version 7 tar archive of its own: no file runs from one floppy to the
# next.  What makes an image of one unreadable as it stands is the order
# Venix used the disk in.  It wrote side 0 from cylinder 0 to 79 and then
# side 1 from cylinder 79 back to 0, where an image file has the two sides
# of each cylinder together; so block L of the archive is, in the image, at
#
#     cylinder c = L / 15 mod 80   (79 - c on side 1)
#     side     s = L / 1200
#     block      = c * 30 + s * 15 + L mod 15
#
# The tar headers are V7's: name[100], mode, uid, gid, size and mtime in
# octal, a checksum, and a link flag ('1' for a hard link, with the name it
# links to).  A directory is an entry whose name ends in '/'.

import os
import sys

BLOCK = 512
SECTORS, SIDES, CYLINDERS = 15, 2, 80
PER_SIDE = SECTORS * CYLINDERS


def unshuffle(image):
    """The floppy's blocks in the order Venix wrote them."""
    if len(image) != BLOCK * PER_SIDE * SIDES:
        sys.exit('not a 1.2 MB floppy image (%d bytes)' % len(image))
    out = bytearray()
    for lblock in range(PER_SIDE * SIDES):
        side, cyl = divmod(lblock // SECTORS, CYLINDERS)
        if side:
            cyl = CYLINDERS - 1 - cyl
        at = (cyl * SIDES * SECTORS + side * SECTORS + lblock % SECTORS) * BLOCK
        out += image[at:at + BLOCK]
    return bytes(out)


def octal(field):
    text = field.split(b'\0')[0].strip()
    return int(text, 8) if text else 0


def members(tar):
    """Yield (name, mode, uid, gid, mtime, link target or None, data)."""
    off = 0
    while off + BLOCK <= len(tar):
        head = tar[off:off + BLOCK]
        off += BLOCK
        if head[0] == 0:
            return
        if octal(head[148:156]) != sum(head[:148]) + 8 * 32 + sum(head[156:]):
            sys.exit('bad tar header at block %d' % (off // BLOCK - 1))
        name = head[:100].split(b'\0')[0].decode('latin-1')
        size = octal(head[124:136])
        link = None
        if head[156:157] == b'1':
            link = head[157:257].split(b'\0')[0].decode('latin-1')
            size = 0
        yield (name, octal(head[100:108]), octal(head[108:116]),
               octal(head[116:124]), octal(head[136:148]), link,
               tar[off:off + size])
        off += (size + BLOCK - 1) // BLOCK * BLOCK


def clean(name):
    return '/' + name.lstrip('./').rstrip('/')


def do_list(paths):
    for path in paths:
        for name, mode, uid, gid, mtime, link, data in members(
                unshuffle(open(path, 'rb').read())):
            kind = 'd' if name.endswith('/') else 'l' if link else '-'
            print('%s%04o %3d %3d %8d %s%s' % (
                kind, mode & 0o7777, uid, gid, len(data), clean(name),
                ' -> ' + clean(link) if link else ''))


def do_extract(dest, paths):
    """Unpack under DEST.  Owners, modes and links are written to
    DEST/.manifest for whoever builds a filesystem from the tree:

        TYPE MODE UID GID MAJOR MINOR MTIME PATH [TARGET]

    TYPE is d, - or l (a hard link to TARGET); the format is the one
    ../pcix/pcixbackup.py writes."""
    files = links = 0
    with open(os.path.join(dest, '.manifest'), 'a') as manifest:
        for path in paths:
            for name, mode, uid, gid, mtime, link, data in members(
                    unshuffle(open(path, 'rb').read())):
                p = clean(name)
                if p == '/':
                    continue
                host = dest + p
                if link:
                    os.makedirs(os.path.dirname(host), exist_ok=True)
                    manifest.write('l 0 0 0 0 0 0 %s %s\n' % (p, clean(link)))
                    links += 1
                    continue
                if name.endswith('/'):
                    os.makedirs(host, exist_ok=True)
                    kind = 'd'
                else:
                    os.makedirs(os.path.dirname(host), exist_ok=True)
                    if os.path.lexists(host):
                        os.chmod(host, 0o600)
                        os.unlink(host)
                    with open(host, 'wb') as f:
                        f.write(data)
                    kind = '-'
                manifest.write('%s %o %d %d 0 0 %d %s\n' % (
                    kind, mode & 0o7777, uid, gid, mtime, p))
                files += 1
    print('%d files, %d links' % (files, links))


def main(argv):
    if len(argv) >= 3 and argv[1] == 'list':
        do_list(argv[2:])
    elif len(argv) >= 4 and argv[1] == 'extract':
        os.makedirs(argv[2], exist_ok=True)
        do_extract(argv[2], argv[3:])
    elif len(argv) == 4 and argv[1] == 'raw':
        with open(argv[3], 'wb') as f:
            f.write(unshuffle(open(argv[2], 'rb').read()))
    else:
        sys.exit('usage: venixtar.py list IMAGE... | extract DIR IMAGE... '
                 '| raw IMAGE OUTPUT')


if __name__ == '__main__':
    main(sys.argv)
