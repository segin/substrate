#!/usr/bin/env python3
import os
import random
import shutil
import string
import subprocess
import sys
import tempfile
from pathlib import Path


SEED = int(os.environ.get("RM_PROP_SEED", "5151"))
ITERATIONS = int(os.environ.get("RM_PROP_ITERS", "40"))


def random_name(rng: random.Random) -> str:
    alphabet = string.ascii_letters + string.digits + "._-"
    return "".join(rng.choice(alphabet) for _ in range(rng.randint(1, 8)))


def populate_tree(root: Path, outside: Path, rng: random.Random, depth: int) -> None:
    if depth == 0:
        return
    for _ in range(rng.randint(1, 4)):
        name = random_name(rng)
        target = root / name
        choice = rng.choice(["file", "dir", "symlink"])
        if choice == "file":
            target.write_text("data", encoding="utf-8")
        elif choice == "dir":
            target.mkdir(parents=True, exist_ok=True)
            populate_tree(target, outside, rng, depth - 1)
        else:
            link_target = outside if rng.random() < 0.5 else Path(".")
            try:
                target.symlink_to(link_target)
            except FileExistsError:
                pass


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: test_property.py /path/to/rm_host", file=sys.stderr)
        return 1

    rng = random.Random(SEED)
    binary = os.path.abspath(sys.argv[1])
    workspace = Path(tempfile.mkdtemp(prefix="rm-prop-"))
    try:
        for iteration in range(ITERATIONS):
            tree = workspace / f"tree_{iteration:03d}"
            outside = workspace / f"outside_{iteration:03d}"
            outside.mkdir()
            (outside / "keep").write_text("keep", encoding="utf-8")
            tree.mkdir()
            populate_tree(tree, outside, rng, rng.randint(1, 3))

            completed = subprocess.run([binary, "-rf", str(tree)],
                                       stdout=subprocess.PIPE,
                                       stderr=subprocess.PIPE,
                                       text=True,
                                       timeout=5)
            if completed.returncode != 0:
                raise AssertionError(completed.stderr)
            if tree.exists():
                raise AssertionError(f"tree still exists: {tree}")
            if not (outside / "keep").exists():
                raise AssertionError("symlink target escaped removal")
        print(f"test_property: ok (seed={SEED}, iterations={ITERATIONS})")
        return 0
    finally:
        shutil.rmtree(workspace, ignore_errors=True)


if __name__ == "__main__":
    raise SystemExit(main())