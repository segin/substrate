#!/usr/bin/env python3
import os
import shutil
import stat
import subprocess
import tempfile
from pathlib import Path


def chmod_bin() -> str:
    return os.environ.get("CHMOD_BIN", "../../bin/chmod/chmod")


def mode(path: Path, follow_symlinks: bool = True) -> int:
    st = path.stat() if follow_symlinks else path.lstat()
    return stat.S_IMODE(st.st_mode)


def run(args):
    return subprocess.run(args, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)


def write_file(path: Path) -> None:
    path.write_text("x", encoding="utf-8")


def test_recursive_p_does_not_follow_internal_symlinks(tmp: Path) -> None:
    root = tmp / "root"
    outside = tmp / "outside"
    root.mkdir()
    outside.mkdir()

    (root / "dir").mkdir()
    write_file(root / "dir" / "inside")
    write_file(outside / "outside_file")
    (root / "link_out").symlink_to("../outside/outside_file")

    os.chmod(root / "dir" / "inside", 0o600)
    os.chmod(outside / "outside_file", 0o640)

    cp = run([chmod_bin(), "-R", "-P", "700", str(root)])
    assert cp.returncode == 0, cp.stderr
    assert mode(root / "dir" / "inside") == 0o700
    assert mode(outside / "outside_file") == 0o640


def test_recursive_h_follows_only_command_line_symlink(tmp: Path) -> None:
    root = tmp / "root_h"
    outside = tmp / "outside_h"
    root.mkdir()
    outside.mkdir()

    (root / "dir").mkdir()
    write_file(root / "dir" / "inside")
    write_file(outside / "outside_file")
    (root / "dir" / "inner_link").symlink_to("../../outside_h/outside_file")
    (tmp / "cmd_link").symlink_to("root_h/dir")

    os.chmod(root / "dir" / "inside", 0o600)
    os.chmod(outside / "outside_file", 0o640)

    cp = run([chmod_bin(), "-R", "-H", "755", str(tmp / "cmd_link")])
    assert cp.returncode == 0, cp.stderr
    assert mode(root / "dir" / "inside") == 0o755
    assert mode(outside / "outside_file") == 0o640


def test_recursive_l_follows_all_symlinks(tmp: Path) -> None:
    root = tmp / "root_l"
    outside = tmp / "outside_l"
    root.mkdir()
    outside.mkdir()

    (root / "dir").mkdir()
    write_file(root / "dir" / "inside")
    write_file(outside / "outside_file")
    (root / "dir" / "inner_link").symlink_to("../../outside_l/outside_file")
    (root / "dir" / "loop").symlink_to("..")

    os.chmod(root / "dir" / "inside", 0o600)
    os.chmod(outside / "outside_file", 0o640)

    cp = run([chmod_bin(), "-R", "-L", "744", str(root)])
    assert cp.returncode == 0, cp.stderr
    assert mode(root / "dir" / "inside") == 0o744
    assert mode(outside / "outside_file") == 0o744


def test_error_reporting_and_force_flag(tmp: Path) -> None:
    missing = tmp / "missing"

    cp = run([chmod_bin(), "644", str(missing)])
    assert cp.returncode != 0
    assert "missing" in cp.stderr

    cp_force = run([chmod_bin(), "-f", "644", str(missing)])
    assert cp_force.returncode != 0
    assert cp_force.stderr == ""


def main() -> int:
    tmp = Path(tempfile.mkdtemp(prefix="chmod-integration-"))
    try:
        test_recursive_p_does_not_follow_internal_symlinks(tmp)
        test_recursive_h_follows_only_command_line_symlink(tmp)
        test_recursive_l_follows_all_symlinks(tmp)
        test_error_reporting_and_force_flag(tmp)
    finally:
        shutil.rmtree(tmp)

    print("integration_chmod: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
