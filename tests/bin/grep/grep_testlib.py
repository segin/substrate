"""Shared helpers for the Substrate grep test suite.

Each test module calls run_suite(cases, grep) where `grep` is the binary path
passed on argv.  egrep/fgrep are resolved as sibling symlinks of `grep`.
"""
import os
import subprocess
import sys
import tempfile


class Grep:
    def __init__(self, grep_path):
        self.grep = os.path.abspath(grep_path)
        d = os.path.dirname(self.grep)
        self.egrep = os.path.join(d, "egrep")
        self.fgrep = os.path.join(d, "fgrep")

    def _bin(self, name):
        return {"grep": self.grep, "egrep": self.egrep,
                "fgrep": self.fgrep}[name]

    def run(self, args, stdin=None, prog="grep", cwd=None):
        """Run grep; return (rc, stdout, stderr) as text."""
        p = subprocess.run(
            [self._bin(prog)] + args,
            input=stdin if stdin is not None else None,
            capture_output=True, text=True, timeout=30, cwd=cwd)
        return p.returncode, p.stdout, p.stderr


class Suite:
    def __init__(self, title):
        self.title = title
        self.passed = 0
        self.failed = 0

    def check(self, desc, got, expected):
        if got == expected:
            self.passed += 1
        else:
            self.failed += 1
            print(f"  FAIL [{self.title}] {desc}")
            print(f"    expected: {expected!r}")
            print(f"    got     : {got!r}")

    def check_rc(self, desc, got_rc, expected_rc):
        self.check(desc + " (rc)", got_rc, expected_rc)

    def done(self):
        total = self.passed + self.failed
        print(f"[{self.title}] {self.passed}/{total} passed")
        return self.failed == 0


def make_files(**named):
    """Create temp files with given contents; return (dir, {name: path})."""
    d = tempfile.mkdtemp(prefix="greptest.")
    paths = {}
    for name, content in named.items():
        p = os.path.join(d, name)
        with open(p, "w", newline="") as f:
            f.write(content)
        paths[name] = p
    return d, paths


def main(title, body):
    if len(sys.argv) < 2:
        print(f"usage: {sys.argv[0]} <grep-binary>", file=sys.stderr)
        sys.exit(2)
    g = Grep(sys.argv[1])
    s = Suite(title)
    body(g, s)
    ok = s.done()
    sys.exit(0 if ok else 1)
