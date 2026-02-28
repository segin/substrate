#!/usr/bin/env python3
import os
import random
import shutil
import stat
import subprocess
import tempfile
from pathlib import Path


SEED = int(os.environ.get("CHMOD_PROP_SEED", "1337"))
ITERATIONS = int(os.environ.get("CHMOD_PROP_ITERS", "60"))


def chmod_bin() -> str:
    return os.environ.get("CHMOD_BIN", "../../bin/chmod/chmod")


def run(args):
    return subprocess.run(args, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)


def mode(path: Path) -> int:
    return stat.S_IMODE(path.stat().st_mode)


def rand_name(rng: random.Random, prefix: str) -> str:
    alphabet = "abcdefghijklmnopqrstuvwxyz0123456789"
    return prefix + "".join(rng.choice(alphabet) for _ in range(6))


def make_random_tree(rng: random.Random, root: Path, outside_file: Path) -> None:
    dirs = [root]
    files = []

    for _ in range(rng.randint(10, 40)):
        parent = rng.choice(dirs)
        choice = rng.random()

        if choice < 0.35:
            d = parent / rand_name(rng, "d")
            d.mkdir(exist_ok=True)
            dirs.append(d)
        elif choice < 0.75:
            f = parent / rand_name(rng, "f")
            f.write_text("x", encoding="utf-8")
            os.chmod(f, rng.randint(0, 0o777))
            files.append(f)
        else:
            link = parent / rand_name(rng, "l")
            if rng.random() < 0.5 and files:
                target = os.path.relpath(str(rng.choice(files)), str(parent))
            else:
                target = os.path.relpath(str(outside_file), str(parent))
            try:
                link.symlink_to(target)
            except FileExistsError:
                pass


def assert_outside_unchanged(bin_path: str, root: Path, outside_file: Path, policy: str, rng: random.Random) -> None:
    before = mode(outside_file)
    target_mode = "u+rwX,go+rX"

    cp = run([bin_path, "-R", policy, target_mode, str(root)])
    assert cp.returncode == 0, f"policy={policy} stderr={cp.stderr}"
    after = mode(outside_file)
    assert before == after, (
        f"outside file mode changed under {policy}: before={oct(before)} after={oct(after)}"
    )


def main() -> int:
    rng = random.Random(SEED)
    bin_path = chmod_bin()

    for i in range(ITERATIONS):
        tmp = Path(tempfile.mkdtemp(prefix=f"chmod-prop-{i:04d}-"))
        try:
            root = tmp / "root"
            outside = tmp / "outside"
            root.mkdir()
            outside.mkdir()

            outside_file = outside / "sentinel"
            outside_file.write_text("outside", encoding="utf-8")
            os.chmod(outside_file, rng.randint(0, 0o777))

            make_random_tree(rng, root, outside_file)

            # With command-line root as a real directory, -P and -H must not
            # follow internal symlinks to outside targets.
            assert_outside_unchanged(bin_path, root, outside_file, "-P", rng)
            assert_outside_unchanged(bin_path, root, outside_file, "-H", rng)
        finally:
            os.chmod(root, 0o755)
            shutil.rmtree(tmp)

    print(f"property_chmod: ok (seed={SEED}, iterations={ITERATIONS})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
