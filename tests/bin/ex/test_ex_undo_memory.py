#!/usr/bin/env python3
"""Undo keeps the change, not the buffer (EXVI-71).

Every change kept a copy of the whole buffer for undo: 200 one-line edits
of a 1 MB, 20,000-line file took ex from 15 MB to 530 MB.  Now a record
holds the lines the change replaced.  The test edits such a file once and
200 times and compares the two peaks (a ratio, so it holds under ASan),
then checks that u still goes back through each change, and that a
command that changes nothing leaves no record for u to undo.

Usage: test_ex_undo_memory.py path/to/ex
"""
import os
import subprocess
import sys
import tempfile

EX = os.path.abspath(sys.argv[1])
LINES = 20000


def peak_kb(path, script):
    """Run ex -s on path with script; its peak resident size in KB."""
    # under ASan, freed memory waits in a quarantine (256 MB) and counts
    env = dict(os.environ)
    env["ASAN_OPTIONS"] = (env.get("ASAN_OPTIONS", "") +
                           ":quarantine_size_mb=1:thread_local_quarantine_size_kb=64")
    proc = subprocess.Popen([EX, "-s", path], stdin=subprocess.PIPE, env=env,
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    proc.stdin.write(script.encode())
    proc.stdin.close()
    _, _, usage = os.wait4(proc.pid, 0)
    return usage.ru_maxrss


def run(path, script):
    return subprocess.run([EX, "-s", path], input=script.encode(),
                          capture_output=True).stdout.decode()


def main():
    fails = 0
    with tempfile.TemporaryDirectory() as d:
        big = os.path.join(d, "big")
        with open(big, "w") as f:
            for i in range(LINES):
                f.write("%050d\n" % i)
        edits = lambda n: "".join("%ds/^/x/\n" % (i * 97 % LINES + 1) for i in range(n))
        one = peak_kb(big, edits(1) + "q!\n")
        many = peak_kb(big, edits(200) + "q!\n")
        if many > one * 2 + 8192:
            print(f"FAIL: 200 edits peaked at {many} KB, one at {one} KB")
            fails += 1

        small = os.path.join(d, "small")
        with open(small, "w") as f:
            f.write("a\nb\nc\nd\n")
        # u, and u again: back through both changes
        out = run(small, "2d\n1s/a/X/\nu\n%p\nu\n%p\nq!\n")
        want = "a\nc\nd\na\nb\nc\nd\n"
        if out != want:
            print(f"FAIL: u twice: {out!r}, want {want!r}")
            fails += 1
        out = run(small, "1s/a/X/\n1p\n=\nu\n1p\nq!\n")
        if out != "X\n4\na\n":
            print(f"FAIL: u after commands that change nothing: {out!r}")
            fails += 1
    if fails:
        return 1
    print("PASS: test_ex_undo_memory")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
