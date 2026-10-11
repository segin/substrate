#!/usr/bin/env python3
"""gnu-diff.py - assemble each line of a corpus with this assembler and
with GNU as, and report every line whose bytes or relocations differ.

    gnu-diff.py [--32|--64] [--opt OPTION]... [--baseline FILE]
                [--write-baseline] [--list] CORPUS

A corpus is one statement to a line, or a few with ` | ` between them
where one needs others before it.  Each line is assembled alone, by
both, into .text; a line's result is its .text bytes and its relocations
(place, type, symbol, addend), or "refused".  A line differs when the
two results are not the same -- one refusing what the other takes is a
difference like any other.

--opt gives an option to this assembler only (GNU as needs none to take
AVX), such as -march=x86-64-v3.

With --baseline FILE the lines in FILE are the differences that are
known.  The run fails when a line not in it differs, and also when a
line in it no longer does: the commit that mends a line takes it out.
--write-baseline writes FILE from what was found.  --list prints every
difference found, known or not.

$AS is this assembler; $GNU_AS, or `as` on $PATH, is GNU's.

Exit status: 0 when the differences are those of the baseline (or, with
none given, always); 1 when they are not; 77 when GNU as is not to be
had, with a line saying so -- the caller is to count that a skip.
"""
import concurrent.futures
import os
import shutil
import struct
import subprocess
import sys
import tempfile


def elf_result(path):
    """The .text bytes and relocations of an object, as one string."""
    b = open(path, 'rb').read()
    if b[:4] != b'\x7fELF':
        return None
    is64 = b[4] == 2
    if is64:
        shoff, = struct.unpack_from('<Q', b, 0x28)
        shentsize, shnum, shstrndx = struct.unpack_from('<HHH', b, 0x3a)
    else:
        shoff, = struct.unpack_from('<I', b, 0x20)
        shentsize, shnum, shstrndx = struct.unpack_from('<HHH', b, 0x2e)
    secs = []
    for i in range(shnum):
        o = shoff + i * shentsize
        if is64:
            name, typ, _, _, off, size, link, info = struct.unpack_from('<IIQQQQII', b, o)
        else:
            name, typ, _, _, off, size, link, info = struct.unpack_from('<IIIIIIII', b, o)
        secs.append((name, typ, off, size, link, info))

    def cstr(tab, at):
        s = secs[tab][2] + at
        return b[s:b.index(b'\0', s)].decode('latin-1')

    names = [cstr(shstrndx, s[0]) for s in secs]
    text = ''
    rels = []
    for i, (_, typ, off, size, link, info) in enumerate(secs):
        if names[i] == '.text' and typ == 1:
            text = b[off:off + size].hex()
        if typ not in (4, 9) or info >= len(names) or names[info] != '.text':
            continue
        symtab = secs[link]
        rela = typ == 4
        entsize = (24 if rela else 16) if is64 else (12 if rela else 8)
        for k in range(size // entsize):
            e = off + k * entsize
            addend = 0
            if is64:
                r_off, r_info = struct.unpack_from('<QQ', b, e)
                if rela:
                    addend, = struct.unpack_from('<q', b, e + 16)
                sym, rtype = r_info >> 32, r_info & 0xffffffff
                st_name, st_info, _, st_shndx = struct.unpack_from('<IBBH', b, symtab[2] + sym * 24)
            else:
                r_off, r_info = struct.unpack_from('<II', b, e)
                if rela:
                    addend, = struct.unpack_from('<i', b, e + 8)
                sym, rtype = r_info >> 8, r_info & 0xff
                st_name, = struct.unpack_from('<I', b, symtab[2] + sym * 16)
                st_info, _, st_shndx = struct.unpack_from('<BBH', b, symtab[2] + sym * 16 + 12)
            # A section symbol has no name of its own: the section's.
            if (st_info & 0xf) == 3 and st_shndx < len(names):
                sname = names[st_shndx]
            else:
                sname = cstr(symtab[4], st_name)
            rels.append('%x:%d:%s%+d' % (r_off, rtype, sname, addend))
    return (text + ' ' + ' '.join(sorted(rels))).strip()


def assemble(job):
    command, line = job
    with tempfile.TemporaryDirectory() as d:
        src = os.path.join(d, 'x.s')
        obj = os.path.join(d, 'x.o')
        with open(src, 'w') as f:
            f.write('\t.text\n\t' + line.replace(' | ', '\n\t') + '\n')
        r = subprocess.run(command + ['-o', obj, src], capture_output=True)
        if r.returncode != 0 or not os.path.exists(obj):
            return 'refused'
        return elf_result(obj) or 'refused'


def main(argv):
    mode = '--32'
    opts = []
    baseline = None
    write_baseline = False
    list_all = False
    corpus = None
    i = 0
    while i < len(argv):
        a = argv[i]
        if a in ('--32', '--64'):
            mode = a
        elif a == '--opt':
            i += 1
            opts.append(argv[i])
        elif a == '--baseline':
            i += 1
            baseline = argv[i]
        elif a == '--write-baseline':
            write_baseline = True
        elif a == '--list':
            list_all = True
        else:
            corpus = a
        i += 1
    if corpus is None:
        sys.stderr.write(__doc__)
        return 2

    ours = os.environ.get('AS')
    if not ours:
        sys.stderr.write('gnu-diff.py: $AS is not set\n')
        return 2
    gnu = os.environ.get('GNU_AS') or shutil.which('as')
    version = ''
    if gnu:
        try:
            version = subprocess.run([gnu, '--version'], capture_output=True, text=True).stdout
        except OSError:
            version = ''
    if 'GNU assembler' not in version:
        print('skipped: GNU as is not installed (looked for %s)' % (os.environ.get('GNU_AS') or '`as` on $PATH'))
        return 77

    lines = []
    for raw in open(corpus):
        s = raw.strip()
        if s and not s.startswith('#'):
            lines.append(s)

    jobs = int(os.environ.get('JOBS') or os.cpu_count() or 2)
    with concurrent.futures.ThreadPoolExecutor(jobs) as pool:
        mine = list(pool.map(assemble, [([ours, mode] + opts, l) for l in lines], chunksize=64))
        theirs = list(pool.map(assemble, [([gnu, mode], l) for l in lines], chunksize=64))

    differing = [l for l, a, b in zip(lines, mine, theirs) if a != b]
    print('%s %s: %d lines, %d differ from GNU as' % (os.path.basename(corpus), mode, len(lines), len(differing)))
    if list_all:
        for l, a, b in zip(lines, mine, theirs):
            if a != b:
                print('  %s\n      here: %s\n      GNU:  %s' % (l, a, b))

    if baseline is None:
        return 0
    if write_baseline:
        with open(baseline, 'w') as f:
            f.write('# Lines of %s that this assembler and GNU as assemble differently.\n' % os.path.basename(corpus))
            f.write('# Written by gnu-diff.py --write-baseline; see test_gnu_differential.sh.\n')
            for l in differing:
                f.write(l + '\n')
        return 0

    known = set()
    for raw in open(baseline):
        s = raw.strip()
        if s and not s.startswith('#'):
            known.add(s)
    result = dict(zip(lines, zip(mine, theirs)))
    new = [l for l in differing if l not in known]
    mended = sorted(l for l in known if l in result and result[l][0] == result[l][1])
    for l in new[:20]:
        print('DIFFERS, and is not in the baseline: %s\n      here: %s\n      GNU:  %s' % ((l,) + result[l]))
    if len(new) > 20:
        print('... and %d more' % (len(new) - 20))
    for l in mended[:20]:
        print('no longer differs (take it out of the baseline): %s' % l)
    if len(mended) > 20:
        print('... and %d more' % (len(mended) - 20))
    return 1 if new or mended else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
