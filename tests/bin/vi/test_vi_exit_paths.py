#!/usr/bin/env python3
"""Every way out of vi leaves cleanly.

vi pointed active_visual, which the atexit handler restoring the terminal
reads, at a structure on exvi_visual_main's stack, and never cleared it:
after every exit the handler read a dead frame (stack-use-after-return
under AddressSanitizer), and could call tcsetattr with whatever was there.
Each return to visual mode also registered the handler again.

On a plain build the old fault is silent; run this on an AddressSanitizer
build (detect_stack_use_after_return) to see it.  The registrations are
counted on a plain build, by an LD_PRELOAD shim on __cxa_atexit.

Usage: test_vi_exit_paths.py path/to/vi
"""
import os
import pty
import signal
import sys
import tempfile
import time

VI = sys.argv[1]
ESC = b"\x1b"

CASES = [
    # name, keys, file afterwards
    (":q!", [b"x", b":q!\r"], "abc\n"),
    ("ZZ", [b"x", b"ZZ"], "bc\n"),
    (":wq", [b"x", b":wq\r"], "bc\n"),
    ("Q to ex, q", [b"Q", b"q!\n"], "abc\n"),
    ("Q, :vi, Q, :vi, ZZ",
     [b"Q", b"vi\n", b"x", b"Q", b"vi\n", b"ZZ"], "bc\n"),
]


def run(path, keys, env=None):
    """Run vi on path through a pty, type keys; return (status, output)."""
    pid, fd = pty.fork()
    if pid == 0:
        os.environ["TERM"] = "xterm"
        os.environ.update(env or {})
        os.execv(VI, [VI, path])
    out = b""
    time.sleep(0.5)
    for k in keys:
        os.write(fd, k)
        time.sleep(0.3)
    end = time.time() + 5
    while time.time() < end:
        try:
            chunk = os.read(fd, 65536)
            if not chunk:
                break
            out += chunk
        except OSError:
            break
    done, status = os.waitpid(pid, os.WNOHANG)
    if done == 0:
        os.kill(pid, signal.SIGKILL)
        os.waitpid(pid, 0)
        status = None
    return status, out


COUNTER = r"""
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>

static int registered;

/* atexit() is __cxa_atexit() with the caller's DSO handle (glibc) */
int __cxa_atexit(void (*fn)(void *), void *arg, void *dso)
{
    static int (*real)(void (*)(void *), void *, void *);

    if (!real)
        real = (int (*)(void (*)(void *), void *, void *))dlsym(RTLD_NEXT, "__cxa_atexit");
    registered++;
    return real(fn, arg, dso);
}

__attribute__((destructor)) static void report(void)
{
    const char *path = getenv("ATEXIT_COUNT");
    FILE *f = path ? fopen(path, "w") : NULL;

    if (f) {
        fprintf(f, "%d\n", registered);
        fclose(f);
    }
}
"""


def registrations(d):
    """The exit handler is registered once, however many times :vi comes
    back to visual mode: atexit's registrations after one Q / :vi round
    trip and after three are as many.  Counted by an LD_PRELOAD shim; an
    ASan build (whose runtime must come first) or a host without cc
    skips it."""
    import shutil
    import subprocess
    cc = shutil.which("cc")
    if not cc:
        print("SKIP: atexit registrations (no cc)")
        return 0
    src = os.path.join(d, "count.c")
    lib = os.path.join(d, "count.so")
    with open(src, "w") as f:
        f.write(COUNTER)
    if subprocess.run([cc, "-shared", "-fPIC", "-o", lib, src, "-ldl"]).returncode != 0:
        print("SKIP: atexit registrations (the shim does not build)")
        return 0
    counts = []
    for trips in (1, 3):
        path = os.path.join(d, "r.txt")
        with open(path, "w") as f:
            f.write("abc\n")
        count = os.path.join(d, "count-%d" % trips)
        keys = [b"Q", b"vi\n"] * trips + [b":q!\r"]
        status, out = run(path, keys, {"LD_PRELOAD": lib, "ATEXIT_COUNT": count})
        if b"runtime does not come first" in out:
            print("SKIP: atexit registrations (an ASan build)")
            return 0
        try:
            with open(count) as f:
                counts.append(int(f.read()))
        except (OSError, ValueError):
            print("FAIL: atexit registrations: no count after %d round trips" % trips)
            return 1
    if counts[0] != counts[1]:
        print("FAIL: the exit handler was registered again on :vi: %d registrations "
              "after one Q / :vi, %d after three" % (counts[0], counts[1]))
        return 1
    return 0


def main():
    fails = 0
    with tempfile.TemporaryDirectory() as d:
        for name, keys, want in CASES:
            path = os.path.join(d, "f.txt")
            with open(path, "w") as f:
                f.write("abc\n")
            status, out = run(path, keys)
            with open(path) as f:
                got = f.read()
            at = out.find(b"AddressSanitizer")
            if at >= 0:
                print("FAIL: %s: %s" % (name, out[at:at + 120]))
                fails += 1
            elif status is None or not os.WIFEXITED(status) or os.WEXITSTATUS(status) != 0:
                print("FAIL: %s: vi did not exit cleanly (status %r)" % (name, status))
                fails += 1
            elif got != want:
                print("FAIL: %s: file is %r, expected %r" % (name, got, want))
                fails += 1
        fails += registrations(d)
    if fails:
        sys.exit(1)
    print("PASS: test_vi_exit_paths")


main()
