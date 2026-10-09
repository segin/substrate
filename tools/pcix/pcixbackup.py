#!/usr/bin/env python3
#
# pcixbackup.py - read the distribution media of IBM PC/IX 1.0.
#
# REQUIRES EXTERNAL MEDIA.  This reads the nineteen 360K floppy images of
# PC/IX, which are not in the repository; see README.md for what to obtain.
# Nothing in any build or test runs it.
#
#   pcixbackup.py list    IMAGE...            what is on the volumes
#   pcixbackup.py extract DIR IMAGE...        unpack them under DIR
#
# IMAGE... is one archive: every volume of it, in order.  The raw images
# (IMA/*.IMA in the WinWorld archive) are what it reads.
#
# THE FORMATS
#
# The first floppy (01MAINT) is a bootable System III filesystem holding the
# standalone installation tools; `extract` on it alone unpacks that.  512-byte
# blocks, the superblock in block 1, 64-byte inodes from block 2 with
# thirteen 3-byte block addresses (low byte first), 16-byte directory
# entries.
#
# Every other floppy is a volume of an INTERACTIVE Systems backup(1)
# archive, the format AIX later kept.  A volume begins with a 72-byte
# header and carries `numwds` 8-byte words in all; what follows the header
# continues the archive exactly where the previous volume stopped, in the
# middle of a file if need be, so the volumes of an archive are read as one
# stream.  The stream is a sequence of records, each starting
#
#     u8  len        length of the record's header, in 8-byte words
#     u8  type
#     u16 magic      0xea6b
#     u16 checksum
#
# with these types on the PC/IX media:
#
#     0  FS_VOLUME   u16 volume, time date, time dumpdate, u32 numwds,
#                    disk[16], fsname[16], user[16], incno
#     1  FS_FINDEX   an index of the inodes on the volume (skipped)
#     3  FS_BITS     u16 nwds, then nwds words of inode bitmap (skipped)
#     8  FS_DINODE   a file by inode number: u16 ino, mode, nlink, uid, gid;
#                    u32 size, atime, mtime, ctime; u16 devmaj, devmin,
#                    rdevmaj, rdevmin; u32 dsize; then dsize bytes of data
#                    rounded up to a word.  A directory's data is the
#                    directory itself, and the names come from there.
#     9  FS_NAME     the same, with the file's path after the fixed part.
#     7  FS_END      end of the archive
#
# The Core system (02CORE1..09CORE8) is one archive by inode, a dump of the
# filesystem the system was built in; each optional subset is an archive by
# name.  All integers are little-endian.

import os
import struct
import sys

MAGIC = 0xEA6B
VOLHDR = 72
FS_VOLUME, FS_FINDEX, FS_BITS, FS_END, FS_DINODE, FS_NAME = 0, 1, 3, 7, 8, 9
IFMT, IFDIR, IFCHR, IFBLK, IFREG, IFIFO = (0o170000, 0o040000, 0o020000,
                                           0o060000, 0o100000, 0o010000)
BSIZE = 512
ROOTINO = 2


class Node:
    """One file off the media."""

    def __init__(self, mode, uid, gid, mtime, rdev, data):
        self.mode, self.uid, self.gid = mode, uid, gid
        self.mtime, self.rdev, self.data = mtime, rdev, data


def is_backup(image):
    return len(image) >= 8 and struct.unpack_from('<H', image, 2)[0] == MAGIC


def stream(images):
    """The volumes' payloads as one run of bytes."""
    out = bytearray()
    for n, image in enumerate(images):
        ln, typ, magic = struct.unpack_from('<BBH', image, 0)
        if magic != MAGIC or typ != FS_VOLUME:
            sys.exit('volume %d does not begin with a volume header' % (n + 1))
        vol = struct.unpack_from('<H', image, 6)[0]
        if vol != n + 1:
            sys.exit('volume %d is out of order (it is number %d)'
                     % (n + 1, vol))
        end = min(struct.unpack_from('<I', image, 16)[0] * 8, len(image))
        out += image[VOLHDR:end]
    return bytes(out)


def records(data):
    """Yield (type, fixed part, name, file data) for each file record."""
    off = 0
    while off + 8 <= len(data):
        ln, typ, magic = struct.unpack_from('<BBH', data, off)
        if magic != MAGIC or ln == 0:
            sys.exit('lost the record structure at offset %#x' % off)
        body = data[off + 6:off + ln * 8]
        off += ln * 8
        if typ == FS_END:
            return
        if typ == FS_BITS:
            off += struct.unpack_from('<H', body, 0)[0] * 8
        elif typ in (FS_DINODE, FS_NAME):
            dsize = struct.unpack_from('<I', body, 34)[0]
            name = body[42:].split(b'\0')[0].decode('latin-1')
            mode = struct.unpack_from('<H', body, 2)[0]
            # Only a regular file has data in an archive by name; by
            # inode a directory has too, and it is where the names are.
            if (mode & IFMT) != IFREG and not (
                    typ == FS_DINODE and (mode & IFMT) == IFDIR):
                dsize = 0
            yield typ, body, name, data[off:off + dsize]
            off += (dsize + 7) & ~7


def node_of(body, data):
    ino, mode, nlink, uid, gid, size, atime, mtime, ctime = struct.unpack_from(
        '<HHHHHIIII', body, 0)
    rmaj, rmin = struct.unpack_from('<HH', body, 30)
    # Most files on the media have the modification time the epoch was in
    # the builders' time zone (18000); the inode change time is the date.
    if mtime < 86400:
        mtime = ctime
    return ino, Node(mode, uid, gid, mtime, (rmaj, rmin), data[:size])


def dirents(data):
    for i in range(0, len(data) - 15, 16):
        ino = struct.unpack_from('<H', data, i)[0]
        name = data[i + 2:i + 16].split(b'\0')[0].decode('latin-1')
        if ino and name not in ('.', '..'):
            yield ino, name


def read_backup(images):
    """{path: Node} and {path: path it is a link to} for one archive."""
    inodes, named, first, links = {}, {}, {}, {}
    for typ, body, name, data in records(stream(images)):
        ino, node = node_of(body, data)
        if typ == FS_DINODE:
            inodes[ino] = node
            continue
        path = '/' + name.lstrip('./')
        if (node.mode & IFMT) != IFDIR and ino in first:
            links[path] = first[ino]
        else:
            named[path] = node
            first[ino] = path
    if inodes:
        walk(inodes, ROOTINO, '', named, first, links, {ROOTINO})
    return named, links


def walk(inodes, ino, path, named, first, links, seen):
    for child, name in dirents(inodes[ino].data):
        node = inodes.get(child)
        if node is None:
            continue                    # not in this dump
        p = path + '/' + name
        if (node.mode & IFMT) == IFDIR:
            named[p] = node
            if child not in seen:
                seen.add(child)
                walk(inodes, child, p, named, first, links, seen)
        elif child in first:
            links[p] = first[child]
        else:
            named[p] = node
            first[child] = p


def read_filesystem(image):
    """The same for a System III filesystem image."""
    def block(n):
        return image[n * BSIZE:(n + 1) * BSIZE]

    def indirect(n, level):
        out = b''
        for (b,) in struct.iter_unpack('<I', block(n)):
            if b:
                out += block(b) if level == 0 else indirect(b, level - 1)
        return out

    def inode(n):
        d = image[2 * BSIZE + (n - 1) * 64:2 * BSIZE + n * 64]
        mode, nlink, uid, gid, size = struct.unpack_from('<HHHHI', d, 0)
        addr = [int.from_bytes(d[12 + i * 3:15 + i * 3], 'little')
                for i in range(13)]
        mtime = struct.unpack_from('<I', d, 56)[0]
        data = b''
        if (mode & IFMT) in (IFREG, IFDIR):
            for i, a in enumerate(addr):
                if len(data) >= size:
                    break
                if i < 10:
                    data += block(a) if a else bytes(BSIZE)
                elif a:
                    data += indirect(a, i - 10)
        rdev = (addr[0] >> 8 & 0xff, addr[0] & 0xff)
        return Node(mode, uid, gid, mtime, rdev, data[:size])

    named, links, first = {}, {}, {}

    def descend(ino, path, seen):
        for child, name in dirents(inode(ino).data):
            node, p = inode(child), path + '/' + name
            if (node.mode & IFMT) == IFDIR:
                named[p] = node
                if child not in seen:
                    seen.add(child)
                    descend(child, p, seen)
            elif child in first:
                links[p] = first[child]
            else:
                named[p] = node
                first[child] = p

    descend(ROOTINO, '', {ROOTINO})
    return named, links


def read_media(paths):
    images = [open(p, 'rb').read() for p in paths]
    if is_backup(images[0]):
        return read_backup(images)
    if len(images) != 1:
        sys.exit('a filesystem image is read by itself')
    return read_filesystem(images[0])


def kind(mode):
    return {IFDIR: 'd', IFCHR: 'c', IFBLK: 'b', IFIFO: 'p'}.get(mode & IFMT,
                                                              '-')


def do_list(paths):
    named, links = read_media(paths)
    for path in sorted(set(named) | set(links)):
        if path in links:
            print('link      %s -> %s' % (path, links[path]))
            continue
        n = named[path]
        print('%s%04o %3d %3d %8d %s' % (kind(n.mode), n.mode & 0o7777,
                                         n.uid, n.gid, len(n.data), path))


def do_extract(dest, paths):
    """Unpack under DEST.  Ownership, modes, devices and links cannot all
    be made by an ordinary user, so they are written to DEST/.manifest,
    one per line, for whoever builds a filesystem from the tree:

        TYPE MODE UID GID MAJOR MINOR MTIME PATH [TARGET]

    TYPE is d, -, c, b, p or l (a hard link to TARGET)."""
    named, links = read_media(paths)
    with open(os.path.join(dest, '.manifest'), 'a') as manifest:
        for path in sorted(named):
            n = named[path]
            host = dest + path
            k = kind(n.mode)
            if k == 'd':
                os.makedirs(host, exist_ok=True)
            else:
                os.makedirs(os.path.dirname(host), exist_ok=True)
            if k == '-':
                if os.path.lexists(host):
                    os.chmod(host, 0o600)
                    os.unlink(host)
                with open(host, 'wb') as f:
                    f.write(n.data)
            manifest.write('%s %o %d %d %d %d %d %s\n' % (
                k, n.mode & 0o7777, n.uid, n.gid, n.rdev[0], n.rdev[1],
                n.mtime, path))
        for path in sorted(links):
            os.makedirs(os.path.dirname(dest + path), exist_ok=True)
            manifest.write('l 0 0 0 0 0 0 %s %s\n' % (path, links[path]))
    print('%d files, %d links' % (len(named), len(links)))


def main(argv):
    if len(argv) >= 3 and argv[1] == 'list':
        do_list(argv[2:])
    elif len(argv) >= 4 and argv[1] == 'extract':
        os.makedirs(argv[2], exist_ok=True)
        do_extract(argv[2], argv[3:])
    else:
        sys.exit(__doc__ or 'usage: pcixbackup.py list|extract ...')


if __name__ == '__main__':
    main(sys.argv)
