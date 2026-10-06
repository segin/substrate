#!/usr/bin/env python3
"""List or extract the cpio archives on a UNIX System V Release 4 floppy.

REQUIRES EXTERNAL MEDIA: this reads distribution floppy images that are
not in the repository.  See build-image.sh in this directory for what to
obtain and how to pass it in.

Two layouts are understood, both built from ASCII ("newc", magic 070701)
cpio archives:

  * a Foundation Set floppy: one archive starting at byte 15360, after a
    volume label, padded to the end of the disk;
  * a package datastream ("# PaCkAgE DaTaStReAm"): a 512-byte header
    block followed by several archives, each starting on a 512-byte
    boundary.

usage: svr4cpio.py list IMAGE
       svr4cpio.py extract IMAGE DEST

Extraction writes regular files, directories and symbolic links.  Modes
are recorded in DEST/.modes ("octal-mode path" per line) rather than
applied, since many files are unreadable to their owner; device nodes are
skipped.

cpio writes a file that has several links once per name and gives the data
to the last of them only; the others are made links to that one.
"""
import os
import shutil
import sys

MAGIC = (b"070701", b"070702")


def members(data):
    """Yield (name, mode, payload, ident) for every member of every archive.

    `ident` is the same for all the names of one multiply-linked file, and
    None for a file with one name.
    """
    pos = 0
    archive = 0
    while pos + 110 <= len(data):
        if data[pos:pos + 6] not in MAGIC:
            pos = (pos // 512 + 1) * 512
            continue
        try:
            f = [int(data[pos + 6 + i * 8:pos + 14 + i * 8], 16)
                 for i in range(13)]
        except ValueError:
            pos = (pos // 512 + 1) * 512
            continue
        mode, size, nlen = f[1], f[6], f[11]
        ident = (archive, f[0]) if f[4] > 1 else None
        name = data[pos + 110:pos + 110 + nlen - 1].decode("latin-1")
        start = (pos + 110 + nlen + 3) & ~3
        payload = data[start:start + size]
        pos = (start + size + 3) & ~3
        if name == "TRAILER!!!":
            pos = ((pos + 511) // 512) * 512
            archive += 1
            continue
        yield name, mode, payload, ident


def clear(path):
    """Make way for a file or link at `path`.

    An earlier volume may have left a directory there -- a boot floppy has
    a /bin of its own where the installed system has a link to /usr/bin --
    and what this archive says replaces it whole.
    """
    if os.path.islink(path) or os.path.isfile(path):
        os.unlink(path)
    elif os.path.isdir(path):
        for root, dirs, _ in os.walk(path):
            for d in dirs:
                os.chmod(os.path.join(root, d), 0o700)
        shutil.rmtree(path)


def main(argv):
    if len(argv) < 3 or argv[1] not in ("list", "extract") or \
            (argv[1] == "extract" and len(argv) != 4):
        sys.stderr.write(__doc__)
        return 2
    with open(argv[2], "rb") as fh:
        data = fh.read()
    if argv[1] == "list":
        for name, mode, payload, _ in members(data):
            print("%06o %8d %s" % (mode, len(payload), name))
        return 0

    dest = argv[3]
    os.makedirs(dest, exist_ok=True)
    count = 0
    holder = {}         # ident -> the path that was given the data
    waiting = []        # (ident, path) of the names that came without it
    with open(os.path.join(dest, ".modes"), "a") as modes:
        for name, mode, payload, ident in members(data):
            rel = os.path.normpath(name.lstrip("/"))
            if rel.startswith(".."):
                continue
            path = os.path.join(dest, rel)
            kind = mode & 0o170000
            if kind == 0o040000:
                os.makedirs(path, exist_ok=True)
            elif kind == 0o100000:
                os.makedirs(os.path.dirname(path), exist_ok=True)
                clear(path)
                if ident is not None and not payload:
                    waiting.append((ident, path))
                else:
                    with open(path, "wb") as out:
                        out.write(payload)
                    if ident is not None:
                        holder[ident] = path
                count += 1
            elif kind == 0o120000:
                os.makedirs(os.path.dirname(path), exist_ok=True)
                clear(path)
                os.symlink(payload.decode("latin-1"), path)
            else:
                continue          # device nodes, fifos
            if kind != 0o120000:
                modes.write("%o %s\n" % (mode & 0o7777, rel))
    for ident, path in waiting:
        if ident in holder:
            os.link(holder[ident], path)
        else:
            open(path, "wb").close()    # an empty file with several names
    print("%d file(s)" % count)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
