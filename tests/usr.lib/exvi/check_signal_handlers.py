#!/usr/bin/env python3
"""The editor's signal handlers call only async-signal-safe functions.

A handler that calls anything else can land inside that same function --
malloc, stdio, the buffer's line lists -- and corrupt it.  SIGINT's
longjmp'd out of whatever was running, and SIGTERM's wrote the buffer
from inside the handler.  Now each only notes the signal in a volatile
sig_atomic_t flag (and SIGTERM's _exit()s on a second one).

This finds every function the sources install as a handler (by
exvi_catch_signal(), signal() or sa_handler), and checks that its body
calls nothing but POSIX's async-signal-safe functions and assigns only
volatile sig_atomic_t variables.

Usage: check_signal_handlers.py path/to/usr.lib/exvi
"""
import os
import re
import sys

# POSIX.1-2024 2.4.3, those a handler here could want
SAFE = {
    "_exit", "_Exit", "write", "read", "close", "open", "kill", "raise",
    "signal", "sigaction", "sigprocmask", "sigemptyset", "sigaddset",
    "tcsetattr", "tcgetattr", "getpid", "abort",
}
KEYWORDS = {"if", "while", "for", "switch", "return", "sizeof", "void", "int"}


def strip_comments(text):
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    return re.sub(r"//[^\n]*", " ", text)


def body_of(text, name):
    m = re.search(r"\b%s\s*\(\s*int\b[^)]*\)\s*\{" % re.escape(name), text)
    if not m:
        return None
    depth = 0
    for i in range(m.end() - 1, len(text)):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                return text[m.end():i]
    return None


def main():
    src_dir = sys.argv[1]
    texts = {}
    for name in sorted(os.listdir(src_dir)):
        if name.endswith(".c") or name.endswith(".h"):
            with open(os.path.join(src_dir, name), encoding="utf-8", errors="replace") as f:
                texts[name] = strip_comments(f.read())
    every = "\n".join(texts.values())

    handlers = set()
    for pat in (r"exvi_catch_signal\(\s*SIG\w+\s*,\s*(\w+)\s*\)",
                r"\bsignal\(\s*SIG\w+\s*,\s*(\w+)\s*\)",
                r"sa_handler\s*=\s*(\w+)\s*;"):
        handlers.update(re.findall(pat, every))
    handlers -= {"SIG_DFL", "SIG_IGN", "fn"}
    if not handlers:
        print("FAIL: no signal handlers found in %s" % src_dir)
        return 1

    fails = 0
    for h in sorted(handlers):
        body = body_of(every, h)
        if body is None and re.search(
                r"\(\s*\*\s*%s\s*\)\s*\(|\b(?:sig_t|sighandler_t)\s+%s\b"
                % (re.escape(h), re.escape(h)), every):
            continue            # a saved handler put back, not a function
        if body is None:
            print("FAIL: %s: installed as a handler, no definition found" % h)
            fails += 1
            continue
        for call in re.findall(r"\b([A-Za-z_]\w*)\s*\(", body):
            if call not in KEYWORDS and call not in SAFE:
                print("FAIL: %s calls %s(), which is not async-signal-safe" % (h, call))
                fails += 1
        for var in re.findall(r"\b([A-Za-z_]\w*)\s*=[^=]", body):
            if not re.search(r"volatile\s+sig_atomic_t\s+(?:\w+\s*(?:=\s*\w+\s*)?,\s*)*%s\b"
                             % re.escape(var), every):
                print("FAIL: %s assigns %s, which is not volatile sig_atomic_t" % (h, var))
                fails += 1
    if fails:
        return 1
    print("PASS: check_signal_handlers (%s)" % ", ".join(sorted(handlers)))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
