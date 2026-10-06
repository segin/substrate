#!/usr/bin/env python3
#
# bar.py - read Sun bar(1) archives, the format of the SunOS 4.0 Sun386i
# distribution floppies.
#
# REQUIRES EXTERNAL MEDIA.  This reads floppy images you supply; none are in
# the repository.  See build-image.sh beside it for what to obtain.
#
#   bar.py title VOLUME            print the volume's title and number
#   bar.py list VOLUME...          list the members of an archive
#   bar.py extract DIR VOLUME...   extract an archive under DIR
#
# An archive spans volumes (floppies); give them in order.  A volume whose
# number is 1 starts a new archive, so several archives may be given at once
# only to `list`.
#
# THE FORMAT
#   Everything is in 512-byte blocks, and a header is tar's with the fields
#   after the checksum laid out differently:
#
#       0   mode[8]   octal, with the S_IFMT bits for a directory
#       8   uid[8]
#       16  gid[8]
#       24  size[12]  bytes of data that follow, as stored
#       36  mtime[12]
#       48  chksum[8]
#       56  rdev[8]
#       64  linkflag  '0' file, '1' link, '2' symbolic link, '3' character
#                     device, '4' block device; meaningless for a directory,
#                     which is a name ending in '/'
#       65  bar_magic[2]   "V\0" in a volume header
#       67  volume_num[4]  decimal
#       71  compressed
#       72  date[12]
#       84  name, NUL, link name, NUL
#
#   Each volume starts with a volume header, whose name is the archive's
#   title and whose size is how many bytes of this volume finish the member
#   the previous one ended in the middle of.  Only data is split that way; a
#   header is never.
#
#   Data is rounded up to a block.  A file written with bar's Z option is
#   stored compress(1)ed, and `size` is then the compressed length; such a
#   file is recognised here by compress's magic number, since the 4.0.1
#   floppies do not set `compressed` in the member's own header.  Expanding
#   uses gzip(1), which reads the format.
#
#   Device nodes are listed and not extracted.  Modes go to DIR/.modes as
#   "MODE UID GID PATH" lines rather than on to the files, so that an
#   unprivileged extraction keeps set-id bits and owners for whatever builds
#   the image.

import os
import subprocess
import sys

BLOCK = 512
Z_MAGIC = b"\x1f\x9d"


def octal(field):
    s = field.split(b"\0")[0].strip()
    return int(s, 8) if s else 0


def rounded(n):
    return (n + BLOCK - 1) // BLOCK * BLOCK


class Member:
    def __init__(self, hdr):
        self.mode = octal(hdr[0:8])
        self.uid = octal(hdr[8:16])
        self.gid = octal(hdr[16:24])
        self.size = octal(hdr[24:36])
        self.flag = hdr[64:65]
        names = hdr[84:].split(b"\0")
        self.name = names[0].decode("latin-1")
        self.link = names[1].decode("latin-1") if len(names) > 1 else ""
        self.data = b""
        if self.name.endswith("/"):
            self.kind = "dir"
        elif self.flag == b"1":
            self.kind = "link"
        elif self.flag == b"2":
            self.kind = "symlink"
        elif self.flag in (b"3", b"4"):
            self.kind = "device"
        else:
            self.kind = "file"
        if self.kind != "file":
            self.size = 0

    def path(self):
        p = os.path.normpath(self.name)
        return "" if p == "." else p.lstrip("/")


def volume_header(vol, name):
    hdr = vol[:BLOCK]
    if len(hdr) < BLOCK or hdr[65:66] != b"V":
        raise SystemExit("bar.py: %s: not a bar volume" % name)
    number = int(hdr[67:71].split(b"\0")[0] or b"0")
    title = hdr[84:].split(b"\0")[0].decode("latin-1")
    return number, title, octal(hdr[24:36])


def members(paths):
    """Yield every member of the archives in `paths`, data included."""
    pending = None          # the member a volume ended in the middle of
    for p in paths:
        with open(p, "rb") as f:
            vol = f.read()
        number, _, cont = volume_header(vol, p)
        if number == 1:
            pending = None
        off = BLOCK
        if pending is not None:
            pending.data += vol[off:off + cont]
            if len(pending.data) >= pending.size:
                pending.data = pending.data[:pending.size]
                yield pending
                pending = None
        elif cont:
            sys.stderr.write("bar.py: %s: continues a volume not given\n" % p)
        off += rounded(cont)
        while off + BLOCK <= len(vol):
            hdr = vol[off:off + BLOCK]
            if not hdr[0:8].strip(b"\0 "):
                break
            m = Member(hdr)
            off += BLOCK
            m.data = vol[off:off + m.size]
            off += rounded(m.size)
            if len(m.data) < m.size:
                pending = m
                break
            yield m
    if pending is not None:
        sys.stderr.write("bar.py: %s: cut short, its last volume is missing\n"
                         % pending.name)


def expand(data):
    if data[:2] != Z_MAGIC:
        return data
    r = subprocess.run(["gzip", "-dc"], input=data, stdout=subprocess.PIPE,
                       stderr=subprocess.DEVNULL)
    # gzip complains of the padding after a stream and still writes it all.
    return r.stdout if r.stdout or r.returncode == 0 else data


def cmd_title(paths):
    for p in paths:
        with open(p, "rb") as f:
            number, title, _ = volume_header(f.read(BLOCK), p)
        print("%d\t%s" % (number, title))


def cmd_list(paths):
    for m in members(paths):
        extra = " -> " + m.link if m.kind in ("link", "symlink") else ""
        print("%-7s %6o %8d %s%s" % (m.kind, m.mode & 0o7777, m.size,
                                     m.name, extra))


def cmd_extract(dest, paths):
    os.makedirs(dest, exist_ok=True)
    links = []
    count = 0
    with open(os.path.join(dest, ".modes"), "a") as modes:
        for m in members(paths):
            rel = m.path()
            if rel.startswith(".."):
                continue
            target = os.path.join(dest, rel) if rel else dest
            if m.kind == "device":
                continue
            if m.kind == "dir":
                os.makedirs(target, exist_ok=True)
            else:
                os.makedirs(os.path.dirname(target), exist_ok=True)
                if os.path.lexists(target) and not os.path.isdir(target):
                    os.unlink(target)
                if m.kind == "symlink":
                    os.symlink(m.link, target)
                    continue
                if m.kind == "link":
                    links.append((m.link, target))
                    continue
                with open(target, "wb") as out:
                    out.write(expand(m.data))
                count += 1
            if rel:
                modes.write("%o %d %d %s\n" % (m.mode & 0o7777, m.uid, m.gid,
                                               rel))
    # A link names the member it is to by the name that one was archived
    # under, which may come later in the archive.
    for src, target in links:
        s = os.path.join(dest, os.path.normpath(src).lstrip("/"))
        if os.path.isfile(s) and not os.path.lexists(target):
            os.link(s, target)
            count += 1
    print(count)


def main(argv):
    if len(argv) >= 3 and argv[1] == "title":
        cmd_title(argv[2:])
    elif len(argv) >= 3 and argv[1] == "list":
        cmd_list(argv[2:])
    elif len(argv) >= 4 and argv[1] == "extract":
        cmd_extract(argv[2], argv[3:])
    else:
        sys.stderr.write(__doc__ or
                         "usage: bar.py title|list|extract ... (see the "
                         "comment at the top)\n")
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
