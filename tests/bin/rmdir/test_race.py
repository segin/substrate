#!/usr/bin/env python3
import os
import shutil
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path


DELAY_US = "50000"
MUTATOR_DELAY = 0.01


def run_case(binary: str, target: Path, mutator, expect_exists: bool) -> None:
    env = os.environ.copy()
    env["RMDIR_TEST_DELAY_US"] = DELAY_US

    worker = threading.Thread(target=mutator)
    worker.start()
    completed = subprocess.run([binary, str(target)],
                               stdin=subprocess.DEVNULL,
                               stdout=subprocess.PIPE,
                               stderr=subprocess.PIPE,
                               text=True,
                               env=env,
                               timeout=5)
    worker.join()

    if completed.returncode == 0:
        raise AssertionError("race case unexpectedly succeeded")
    if target.exists() != expect_exists:
        raise AssertionError(f"unexpected target existence for {target}")


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: test_race.py /path/to/rmdir_host", file=sys.stderr)
        return 1

    binary = os.path.abspath(sys.argv[1])
    workspace = Path(tempfile.mkdtemp(prefix="rmdir-race-"))
    try:
        nonempty = workspace / "nonempty"
        nonempty.mkdir()

        def add_file() -> None:
            time.sleep(MUTATOR_DELAY)
            (nonempty / "file").write_text("x", encoding="utf-8")

        run_case(binary, nonempty, add_file, True)

        nested = workspace / "nested"
        nested.mkdir()

        def add_directory() -> None:
            time.sleep(MUTATOR_DELAY)
            (nested / "child").mkdir()

        run_case(binary, nested, add_directory, True)

        removed = workspace / "removed"
        removed.mkdir()

        def remove_directory() -> None:
            time.sleep(MUTATOR_DELAY)
            os.rmdir(removed)

        run_case(binary, removed, remove_directory, False)

        print("test_race: ok")
        return 0
    finally:
        shutil.rmtree(workspace, ignore_errors=True)


if __name__ == "__main__":
    raise SystemExit(main())