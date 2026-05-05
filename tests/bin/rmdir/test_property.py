#!/usr/bin/env python3
import os
import random
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


SEED = int(os.environ.get("RMDIR_PROP_SEED", "7171"))
ITERATIONS = int(os.environ.get("RMDIR_PROP_ITERS", "50"))


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: test_property.py /path/to/rmdir_host", file=sys.stderr)
        return 1

    rng = random.Random(SEED)
    binary = os.path.abspath(sys.argv[1])
    workspace = Path(tempfile.mkdtemp(prefix="rmdir-prop-"))
    try:
        for iteration in range(ITERATIONS):
            depth = rng.randint(1, 5)
            current = workspace / f"case_{iteration:03d}"
            current.mkdir()
            for level in range(depth):
                current = current / f"d{level}"
                current.mkdir()

            completed = subprocess.run(
                                       [binary, "-p", os.path.relpath(current, workspace)],
                                       cwd=workspace,
                                       stdin=subprocess.DEVNULL,
                                       stdout=subprocess.PIPE,
                                       stderr=subprocess.PIPE,
                                       text=True,
                                       timeout=5)
            if completed.returncode != 0:
                raise AssertionError(completed.stderr)
            if (workspace / f"case_{iteration:03d}").exists():
                raise AssertionError("ancestor chain still exists")
        print(f"test_property: ok (seed={SEED}, iterations={ITERATIONS})")
        return 0
    finally:
        shutil.rmtree(workspace, ignore_errors=True)


if __name__ == "__main__":
    raise SystemExit(main())