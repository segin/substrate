#!/usr/bin/env python3
import os
import random
import shutil
import stat
import string
import subprocess
import sys
import tempfile
from pathlib import Path


SEED = int(os.environ.get("MKDIR_PROP_SEED", "4242"))
ITERATIONS = int(os.environ.get("MKDIR_PROP_ITERS", "60"))


def run(args, cwd=None):
    return subprocess.run(args, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)


def random_segment(rng: random.Random) -> str:
    alphabet = string.ascii_letters + string.digits + "._-"
    return "".join(rng.choice(alphabet) for _ in range(rng.randint(1, 8)))


def random_mode(rng: random.Random) -> int:
    return rng.randint(0, 0o777)


def reset_permissions(root: Path) -> None:
    for path, dirnames, filenames in os.walk(root, topdown=False):
        current = Path(path)
        os.chmod(current, 0o700)
        for name in filenames:
            os.chmod(current / name, 0o600)
        for name in dirnames:
            os.chmod(current / name, 0o700)


def test_random_paths(bin_path: str, rng: random.Random, tmp: Path) -> None:
    for _ in range(ITERATIONS):
        parts = [random_segment(rng) for _ in range(rng.randint(1, 4))]
        target = tmp.joinpath(*parts)
        cp = run([bin_path, "-p", str(target)])
        assert cp.returncode == 0, cp.stderr
        assert target.is_dir(), target


def test_random_numeric_modes(bin_path: str, rng: random.Random, tmp: Path) -> None:
    for i in range(ITERATIONS):
        mode = random_mode(rng)
        target = tmp / f"mode_{i:04d}"
        cp = run([bin_path, "-m", f"{mode:o}", str(target)])
        assert cp.returncode == 0, cp.stderr
        got = stat.S_IMODE(target.stat().st_mode)
        assert got == mode, (oct(got), oct(mode))


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: test_property.py /path/to/mkdir_host", file=sys.stderr)
        return 1

    rng = random.Random(SEED)
    tmp = Path(tempfile.mkdtemp(prefix="mkdir-prop-"))
    try:
        test_random_paths(sys.argv[1], rng, tmp)
        test_random_numeric_modes(sys.argv[1], rng, tmp)
    finally:
        reset_permissions(tmp)
        shutil.rmtree(tmp)

    print(f"test_property: ok (seed={SEED}, iterations={ITERATIONS})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())