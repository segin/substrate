#!/usr/bin/env python3
import os
import random
import shutil
import subprocess
import sys
import string
import tempfile
from pathlib import Path

SEED = int(os.environ.get("CHOWN_PROP_SEED", "4242"))
ITERATIONS = int(os.environ.get("CHOWN_PROP_ITERS", "60"))


def run(args, cwd=None):
    return subprocess.run(args, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)


def random_segment(rng: random.Random) -> str:
    alphabet = string.ascii_letters + string.digits + "._-"
    return "".join(rng.choice(alphabet) for _ in range(rng.randint(1, 8)))


def owner_at(path: Path) -> tuple:
    st = path.stat()
    return (st.st_uid, st.st_gid)


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: test_property.py /path/to/chown_host", file=sys.stderr)
        return 1

    rng = random.Random(SEED)
    tmp = Path(tempfile.mkdtemp(prefix="chown-prop-"))
    try:
        # Test 1: numeric uid changes
        for i in range(ITERATIONS):
            uid = rng.randint(0, 65535)
            filepath = tmp / f"uid_{i:04d}"
            filepath.touch()
            cp = run([sys.argv[1], str(uid), str(filepath)])
            assert cp.returncode == 0, f"chown uid={uid}: {cp.stderr}"
            got = owner_at(filepath)
            assert got == (uid, 0), (filepath, got, (uid, 0))

        # Test 2: numeric gid changes
        for i in range(ITERATIONS):
            gid = rng.randint(0, 65535)
            filepath = tmp / f"gid_{i:04d}"
            filepath.touch()
            cp = run([sys.argv[1], f":{gid}", str(filepath)])
            assert cp.returncode == 0, f"chown gid={gid}: {cp.stderr}"
            got = owner_at(filepath)
            assert got[1] == gid, (filepath, got, gid)

        # Test 3: uid:gid together
        for i in range(ITERATIONS):
            uid = rng.randint(0, 65535)
            gid = rng.randint(0, 65535)
            filepath = tmp / f"both_{i:04d}"
            filepath.touch()
            cp = run([sys.argv[1], f"{uid}:{gid}", str(filepath)])
            assert cp.returncode == 0, f"chown uid:gid: {cp.stderr}"
            got = owner_at(filepath)
            assert got == (uid, gid), (filepath, got, (uid, gid))

        # Test 4: recursive ownership
        root = tmp / "recurse"
        root.mkdir()
        (root / "sub").mkdir()
        (root / "sub" / "file").touch()
        cp = run([sys.argv[1], "-R", "0:0", str(root)])
        assert cp.returncode == 0, f"recursive chown: {cp.stderr}"
        assert owner_at(root) == (0, 0)
        assert owner_at(root / "sub") == (0, 0)
        assert owner_at(root / "sub" / "file") == (0, 0)

        # Test 5: symlink -h
        link = tmp / "symlink"
        link.symlink_to("nonexistent")
        cp = run([sys.argv[1], "-h", "0:0", str(link)])
        assert cp.returncode == 0, f"symlink -h: {cp.stderr}"
        assert owner_at(link) == (0, 0)

    finally:
        shutil.rmtree(tmp)

    print(f"test_property: ok (seed={SEED}, iterations={ITERATIONS})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
