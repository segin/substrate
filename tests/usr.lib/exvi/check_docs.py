#!/usr/bin/env python3
"""ex.1, vi.1 and exvi_conformance.md name only what the editor has.

The 2026-11-06 exvi audit's EXVI-81.1: the pages documented options and
behaviour the editor lacked, and the conformance matrix later called
implemented commands missing.  A reader of the code is the full check;
this one catches the names drifting:

  - each command ex.1's command list names is one exvi.c's dispatch
    takes (a full name, or a prefix of one no shorter than its
    abbreviation); punctuation commands are left to the tests
  - each option ex.1 lists, and each SET OPTIONS entry of ex.1 and vi.1,
    is in exvi_options.c's table, under that name and abbreviation; and
    each option in the table is named in ex.1
  - each flag the pages' synopses and OPTIONS give is one exvi.c's getopt
    takes, and each getopt flag is documented in both pages
  - no line of the conformance matrix calls a command the dispatch takes
    missing

Usage: check_docs.py path/to/tree-top
"""
import os
import re
import sys

TOP = sys.argv[1]
fails = 0


def fail(msg):
    global fails
    print("FAIL: %s" % msg)
    fails += 1


def read(rel):
    try:
        with open(os.path.join(TOP, rel), encoding="utf-8", errors="replace") as f:
            return f.read()
    except OSError:
        fail("no %s" % rel)
        return ""


exvi = read("usr.lib/exvi/exvi.c")
options_c = read("usr.lib/exvi/exvi_options.c")
ex1 = read("usr.man/man1/ex.1")
vi1 = read("usr.man/man1/vi.1")
matrix = read("docs/specs/exvi_conformance.md")

# --- commands
dispatch = re.findall(r'match_command\(\s*cmd\s*,\s*"([^"]+)"\s*,\s*(?:"([^"]*)"|NULL)', exvi)


def accepted(name):
    name = name.rstrip("!")
    if name in ("k", "z"):
        return re.search(r"cmd\[0\] == '%s'" % name, exvi) is not None
    for full, abbr in dispatch:
        if name == full or name == abbr:
            return True
        if abbr and name.startswith(abbr) and full.startswith(name):
            return True
    return False


m = re.search(r"The current supported command contract includes:\n\.Bl[^\n]*\n(.*?)\n\.El", ex1, re.S)
if not m:
    fail("ex.1 has no command list")
else:
    for name in re.findall(r"^\.Ic\s+(\S+)", m.group(1), re.M):
        if re.fullmatch(r"[a-z]+!?", name) and not accepted(name):
            fail("ex.1 lists the command %s, which the dispatch does not take" % name)

for line in matrix.splitlines():
    if re.search(r"\bmissing\b|\bnot implemented\b", line):
        for name in re.findall(r"`([a-z]+)`", line):
            if accepted(name):
                fail("the conformance matrix calls %s missing: %s" % (name, line[:80]))

# --- options
table = {}     # name: its abbreviation and alias
for name, abbr, alias in re.findall(
        r'\{\s*"([a-z]+)"\s*,\s*(?:"([a-z]*)"|NULL)\s*,\s*(?:"([a-z]*)"|NULL)', options_c):
    table[name] = {a for a in (abbr, alias) if a}
if not table:
    fail("no option table in exvi_options.c")
m = re.search(r"Every option POSIX lists is accepted.*?reported:\n(.*?)\.\n", ex1, re.S)
listed = re.findall(r"([a-z]+)(?:\s+\(([a-z]+)\))?", m.group(1)) if m else []
if not listed:
    fail("ex.1 has no option list")
for name, abbr in listed:
    if name == "and":
        continue
    if name not in table:
        fail("ex.1 lists the option %s, which the table does not have" % name)
    elif abbr and abbr not in table[name]:
        fail("ex.1 gives %s the abbreviation %s, the table %s" % (name, abbr, table[name]))
for page, text in (("ex.1", ex1), ("vi.1", vi1)):
    for entry in re.findall(r"^\.It Ic (.+)$", text, re.M):
        names = [w.rstrip("=") for w in re.findall(r"[a-z]+=?", entry)]
        if names and names[0] in table:
            for alias in names[1:]:
                if alias not in table[names[0]]:
                    fail("%s gives %s the abbreviation %s, the table %s"
                         % (page, names[0], alias, table[names[0]]))
for name in table:
    if not re.search(r"\b%s\b" % name, ex1):
        fail("the option %s is not named in ex.1" % name)

# --- flags
m = re.search(r'getopt\(argc, argv, "\+?([^"]*)"\)', exvi)
flags = set(re.sub(r"[:+]", "", m.group(1))) if m else set()
if not flags:
    fail("no getopt string in exvi.c")
for page, text in (("ex.1", ex1), ("vi.1", vi1)):
    synopsis = text.split(".Sh DESCRIPTION")[0]
    given = set("".join(re.findall(r"\bFl ([A-Za-z]+)", synopsis)))
    options = text.split(".Sh OPTIONS")[1].split(".Sh ")[0] if ".Sh OPTIONS" in text else ""
    documented = set(re.findall(r"^\.It Fl ([A-Za-z])\b", options, re.M))
    for f in sorted(given | documented):
        if f not in flags:
            fail("%s documents -%s, which getopt does not take" % (page, f))
    for f in sorted(flags - documented):
        fail("%s does not document -%s" % (page, f))

if fails:
    sys.exit(1)
print("PASS: check_docs")
