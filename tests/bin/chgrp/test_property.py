#!/usr/bin/env python3
import os
import random
import shutil
import subprocess
import sys
import string
import tempfile
from pathlib import Path

SEED = int(os.environ.get("CHGRP_PROP_SEED", "4242"))
ITERATIONS = int(os.environ.get("CHGRP_PROP_ITERS", "60"))


def run(args, cwd=None):
    return subprocess.run(args, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)


def random_segment(rng: random.Random) -> str:
    alphabet = string.ascii_letters + string.digits + "._-"
    return "".join(rng.choice(alphabet) for _ in range(rng.randint(1, 8)))


def group_at(path: Path) -> int:
    st = path.stat()
    return st.st_gid


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: test_property.py /path/to/chgrp_host", file=sys.stderr)
        return 1

    rng = random.Random(SEED)
    tmp = Path(tempfile.mkdtemp(prefix="chgrp-prop-"))
    try:
        # Test 1: numeric gid changes
        for i in range(ITERATIONS):
            gid = rng.randint(0, 65535)
            filepath = tmp / f"gid_{i:04d}"
            filepath.touch()
            cp = run([sys.argv[1], str(gid), str(filepath)])
            assert cp.returncode == 0, f"chgrp gid={gid}: {cp.stderr}"
            got = group_at(filepath)
            assert got == gid, (filepath, got, gid)

        # Test 2: recursive group change
        root = tmp / "recurse"
        root.mkdir()
        (root / "sub").mkdir()
        (root / "sub" / "file").touch()
        cp = run([sys.argv[1], "-R", "0", str(root)])
        assert cp.returncode == 0, f"recursive chgrp: {cp.stderr}"
        assert group_at(root) == 0
        assert group_at(root / "sub") == 0
        assert group_at(root / "sub" / "file") == 0

        # Test 3: symlink -h
        link = tmp / "symlink"
        link.symlink_to("nonexistent")
        cp = run([sys.argv[1], "-h", "0", str(link)])
        assert cp.returncode == 0, f"symlink -h: {cp.stderr}"
        assert group_at(link) == 0

    finally:
        shutil.rmtree(tmp)

    print(f"test_property: ok (seed={SEED}, iterations={ITERATIONS})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
