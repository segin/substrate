#!/usr/bin/env python3
"""
mkelfwrap.py - write an i386 Substrate ELF whose one PT_LOAD is not
page-congruent: p_vaddr is 0x100 into its page but p_offset is 0x80.

The ELF loader's file-mapping fast path took the page's file offset as
p_offset - (p_vaddr & 0xfff), which wraps below zero here; the page was
then read from far past the end of the file (zeros), and the program
crashed on its first instruction.  Loaded correctly, it exits with 42.
(Linux refuses such a segment outright; this is Substrate's loader.)

Usage: mkelfwrap.py <output>
"""
import struct
import sys

VADDR = 0x08049100
OFFSET = 0x80

# Native exit(42): the argument on the stack above a dummy return address,
# the number in %eax.  push $42 ; push $0 ; mov $1, %eax ; int $0x80
code = bytes([0x6A, 42, 0x6A, 0, 0xB8, 1, 0, 0, 0, 0xCD, 0x80])

ehdr = struct.pack(
    "<16sHHIIIIIHHHHHH",
    b"\x7fELF\x01\x01\x01\x40" + bytes(8),  # 32-bit, LSB, ELFOSABI_SUBSTRATE
    2, 3, 1,                            # ET_EXEC, EM_386, EV_CURRENT
    VADDR,                              # e_entry
    52, 0,                              # e_phoff, e_shoff
    0, 52, 32, 1, 40, 0, 0)
phdr = struct.pack(
    "<IIIIIIII",
    1,                                  # PT_LOAD
    OFFSET, VADDR, VADDR,
    len(code), len(code),               # filesz == memsz: the fast path
    5,                                  # PF_R | PF_X
    0x1000)

image = ehdr + phdr
image += bytes(OFFSET - len(image)) + code
with open(sys.argv[1], "wb") as f:
    f.write(image)
