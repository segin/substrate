#!/usr/bin/env python3
"""mkcoff.py - put Sun386i COFF headers on a flat program image.

    mkcoff.py IMAGE.bin SYMBOLS.nm OUTPUT

IMAGE.bin is the program from its first text byte on, as `objcopy -O
binary` writes what sun386.ld lays out; SYMBOLS.nm is `nm` of the same
link, for where the text, data and bss begin and end.  OUTPUT is a file
substrate's COFF loader takes for a Sun386i program: an i386 COFF file
header, an optional header with magic 0413, and .text, .data and .bss
section headers, 0xd0 bytes in all, then the image.

Runs on the build host and needs nothing but Python.
"""

import struct
import sys

HEADERS = 0xd0              # where the text begins in the file
TEXT_PAGE = 0x1000          # where the file's first page is mapped
COFF_I386 = 0x14c
ZMAGIC = 0o413
F_FLAGS = 0x000f            # no relocations, executable, no lines, no symbols
STYP_TEXT, STYP_DATA, STYP_BSS = 0x20, 0x40, 0x80


def main():
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    image = open(sys.argv[1], "rb").read()
    sym = {}
    for line in open(sys.argv[2]):
        parts = line.split()
        if len(parts) == 3:
            sym[parts[2]] = int(parts[0], 16)
    text, etext = sym["_text"], sym["_etext"]
    data, edata, end = sym["_data"], sym["_edata"], sym["_end"]
    if text != TEXT_PAGE + HEADERS:
        sys.exit("mkcoff: text must start at %#x" % (TEXT_PAGE + HEADERS))
    if len(image) != edata - text:
        sys.exit("mkcoff: image is %d bytes, text to edata is %d"
                 % (len(image), edata - text))

    def section(name, vaddr, size, flags, in_file=True):
        return struct.pack("<8sIIIIIIHHI", name, vaddr, vaddr, size,
                           vaddr - TEXT_PAGE if in_file else 0, 0, 0, 0, 0,
                           flags)

    header = struct.pack("<HHIIIHH", COFF_I386, 3, 0, 0, 0, 28, F_FLAGS)
    header += struct.pack("<HHIIIIII", ZMAGIC, 0, etext - text,
                          edata - data, end - edata, sym["_start"], text,
                          data)
    header += section(b".text", text, etext - text, STYP_TEXT)
    header += section(b".data", data, edata - data, STYP_DATA)
    header += section(b".bss", edata, end - edata, STYP_BSS, in_file=False)
    header += bytes(HEADERS - len(header))
    with open(sys.argv[3], "wb") as out:
        out.write(header + image)


if __name__ == "__main__":
    main()
