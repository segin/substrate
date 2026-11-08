#!/usr/bin/env python3
"""Invariants two of the 2026-11-06 kernel audit's fixes rest on, which no
run of the code can show:

REQ-SYS-VM-003 (second part): the UMA zone counters -- uz_allocs,
uz_frees, uz_count, uz_max -- change only in uma_count_alloc() and
uma_count_free(), which hold the slab lock.  They were read-modify-written
in the allocation and free paths with interrupts back on, and on the
per-CPU paths with only this CPU's interrupts off: a kfree() from the
network RX path, or another CPU, could interleave and lose an update, and
uz_allocs and uz_frees are 64-bit, which an i486 has no atomic add for.
The race needs an interrupt between the halves of an add, which no host
test can arrange, so the check is that no other code writes them.

REQ-SYS-STYLE-001: the module types the audit found defined in .c files
-- vm_vnode_pager_t, vm_device_pager_t, shared_file_object_entry_t,
afi_sock_t -- are defined in headers, and vm_swapon() / vm_swapoff() are
declared once (in vm_swap.h), not twice.

Run from tests/sys (the sources are ../../sys); installed under the test's
name as a HOST_TESTS entry.
"""
import os
import re
import sys

SYS = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "sys")
fails = 0


def fail(msg):
    global fails
    print("FAIL: %s" % msg)
    fails += 1


def read(rel):
    try:
        with open(os.path.join(SYS, rel), encoding="utf-8", errors="replace") as f:
            return f.read()
    except OSError:
        fail("no %s" % rel)
        return ""


def strip_comments(text):
    text = re.sub(r"/\*.*?\*/", lambda m: "\n" * m.group(0).count("\n"), text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def body_span(text, name):
    """(start, end) offsets of function name's body, or None."""
    m = re.search(r"\b%s\s*\([^)]*\)\s*\{" % re.escape(name), text)
    if not m:
        return None
    depth = 0
    for i in range(m.end() - 1, len(text)):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                return (m.end(), i)
    return None


# --- REQ-SYS-VM-003: the zone counters change only under the slab lock
uma = strip_comments(read("vm/uma_core.c"))
counters = r"uz_(?:allocs|frees|count|max)\b"
write = re.compile(r"(?:\+\+|--)\s*[\w>.-]*%s|%s\s*(?:\+\+|--|[-+*/|&]?(?<![<>!=])=(?!=))"
                   % (counters, counters))
spans = [s for s in (body_span(uma, "uma_count_alloc"), body_span(uma, "uma_count_free")) if s]
if len(spans) != 2:
    fail("uma_core.c has no uma_count_alloc() and uma_count_free()")
for m in write.finditer(uma):
    if not any(a <= m.start() < b for a, b in spans):
        line = uma.count("\n", 0, m.start()) + 1
        fail("uma_core.c:%d changes a zone counter outside uma_count_alloc/free: %s"
             % (line, uma[m.start():uma.find("\n", m.start())].strip()))
for a, b in spans:
    if "uma_slab_lock_acquire" not in uma[a:b] or "uma_slab_lock_release" not in uma[a:b]:
        fail("uma_count_alloc/free does not hold the slab lock")

# --- REQ-SYS-STYLE-001: the types are in headers; vm_swapon declared once
for typ, header, source in (("vm_vnode_pager_t", "vm/vm_pager.h", "vm/vm_pager.c"),
                            ("vm_device_pager_t", "vm/vm_pager.h", "vm/vm_pager.c"),
                            ("shared_file_object_entry_t", "vm/vm_object.h", "vm/vm_syscalls.c"),
                            ("afi_sock_t", "include/net/af_inet_var.h", "net/af_inet.c")):
    defined = re.compile(r"\}\s*%s\s*;" % typ)
    if not defined.search(strip_comments(read(header))):
        fail("%s is not defined in %s" % (typ, header))
    if defined.search(strip_comments(read(source))):
        fail("%s is defined in %s, a .c file" % (typ, source))
for fn in ("vm_swapon", "vm_swapoff"):
    decls = []
    for root, _, files in os.walk(SYS):
        for name in files:
            if name.endswith(".h"):
                path = os.path.join(root, name)
                try:
                    with open(path, encoding="utf-8", errors="replace") as f:
                        text = strip_comments(f.read())
                except OSError:
                    continue            # a dangling link
                if re.search(r"\b\w[\w\s*]*\b%s\s*\([^)]*\)\s*;" % fn, text):
                    decls.append(os.path.relpath(path, SYS))
    if decls != ["vm/vm_swap.h"]:
        fail("%s() is declared in %s, not once in vm/vm_swap.h" % (fn, decls))

if fails:
    sys.exit(1)
print("PASS: host_test_audit_invariants")
