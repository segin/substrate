#!/usr/bin/env python3
"""Loading a file and :g take time linear in the lines (EXVI-72).

Every insert looked its neighbour up from the top of the line array, so
loading was quadratic (100,000 lines in 1.9 s), and :g went back to the
top after each command (g/./s/$/x/ on 25,000 lines in 2.4 s).  The test
times each at N and 5N lines and fails when the time grows like N squared
rather than N -- a ratio, so a slow host or ASan does not matter.  It also
checks :g still reaches every marked line when its commands delete or move
lines.

Usage: test_ex_scaling.py path/to/ex
"""
import os
import subprocess
import sys
import tempfile
import time

EX = os.path.abspath(sys.argv[1])
N = 6000


def timed(path, script):
    best = None
    for _ in range(3):
        start = time.monotonic()
        subprocess.run([EX, "-s", path], input=script.encode(),
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        took = time.monotonic() - start
        best = took if best is None else min(best, took)
    return best


def output(path, script):
    return subprocess.run([EX, "-s", path], input=script.encode(),
                          capture_output=True).stdout.decode()


def main():
    fails = 0
    with tempfile.TemporaryDirectory() as d:
        files = {}
        for n in (N, 5 * N):
            files[n] = os.path.join(d, f"l{n}")
            with open(files[n], "w") as f:
                f.write("".join(f"{i}\n" for i in range(1, n + 1)))
        for what, script in (("load", "q!\n"), ("g/./s/$/x/", "g/./s/$/x/\nq!\n"),
                             ("g/./d", "g/./d\nq!\n")):
            small = timed(files[N], script)
            large = timed(files[5 * N], script)
            # linear: about 5 (plus the start-up cost both share); quadratic: 25
            if large > 12 * small + 0.05:
                print(f"FAIL: {what}: {small:.3f}s for {N} lines, {large:.3f}s for {5 * N}")
                fails += 1

        f = os.path.join(d, "g")
        with open(f, "w") as fh:
            fh.write("a1\nb\na2\nb\na3\n")
        for script, want in (("g/a/d\n%p\nq!\n", "b\nb\n"),
                             ("g/a/m0\n%p\nq!\n", "a3\na2\na1\nb\nb\n"),
                             ("g/b/-1d\n%p\nq!\n", "b\nb\na3\n"),
                             ("g/a/t0\n%p\nq!\n", "a3\na2\na1\na1\nb\na2\nb\na3\n")):
            got = output(f, script)
            if got != want:
                print(f"FAIL: {script.splitlines()[0]}: {got!r}, want {want!r}")
                fails += 1
    if fails:
        return 1
    print("PASS: test_ex_scaling")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
