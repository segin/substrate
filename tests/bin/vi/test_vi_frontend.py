#!/usr/bin/env python3

import importlib.util
import os
import subprocess
import sys
import tempfile


def require(cond, msg):
    if not cond:
        print(f"FAIL: {msg}", file=sys.stderr)
        raise SystemExit(1)


def load_pty_helpers():
    helper_path = os.path.join(os.path.dirname(__file__), "test_vi_pty.py")
    spec = importlib.util.spec_from_file_location("test_vi_pty", helper_path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def main():
    if len(sys.argv) != 2:
        print(f"usage: {sys.argv[0]} /path/to/vi", file=sys.stderr)
        return 2

    vi_path = sys.argv[1]
    helpers = load_pty_helpers()

    with tempfile.NamedTemporaryFile("w", delete=False) as f:
        f.write("one\n")
        path = f.name
    try:
        proc = subprocess.run([vi_path, path], input=b"",
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        require(proc.returncode == 1, f"vi non-tty status mismatch: {proc.returncode}")
        require(proc.stderr.decode("latin1", "replace") == "vi: visual mode requires a terminal.\n",
                f"vi non-tty stderr mismatch: {proc.stderr!r}")
    finally:
        os.unlink(path)

    proc = subprocess.run([vi_path, "-r"], input=b"",
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    require(proc.returncode == 1, f"vi -r missing-file status mismatch: {proc.returncode}")
    require(proc.stdout == b"", f"vi -r missing-file stdout mismatch: {proc.stdout!r}")
    require(proc.stderr.decode("latin1", "replace") == "vi: -r requires a file operand\n",
            f"vi -r missing-file stderr mismatch: {proc.stderr!r}")

    exit_code, decoded, saved = helpers.run_vi_session(
        vi_path,
        "one\n",
        [b":q!\r"],
        final_keys=None,
        extra_args=["-r"],
        extra_files={"buffer.txt.recover": "RECOVERED\n"},
    )
    require(exit_code == 0, f"vi -r recovery exited with status {exit_code}")
    require("recovered, 1 lines" in decoded, "vi -r missing recovered status")
    require(saved == "one\n", f"vi -r unexpectedly modified file: {saved!r}")

    exit_code, decoded, saved = helpers.run_vi_session(
        vi_path,
        "one\n",
        [b":q!\r"],
        final_keys=None,
        extra_args=["-R"],
    )
    require(exit_code == 0, f"readonly vi exited with status {exit_code}")
    require("[Readonly]" in decoded, "readonly vi missing readonly status")
    require(saved == "one\n", f"readonly vi unexpectedly modified file: {saved!r}")

    exit_code, decoded, saved = helpers.run_vi_session(
        vi_path,
        "one\n",
        [b"i", b"X", b"\x1b", b":wq\r", b":q!\r"],
        final_keys=None,
        extra_args=["-R"],
    )
    require(exit_code == 0, f"readonly blocked-write vi exited with status {exit_code}")
    require("[Readonly]" in decoded, "readonly blocked-write vi missing readonly status")
    require("File is read only (add ! to override)" in decoded,
            "readonly blocked-write vi missing diagnostic")
    require(saved == "one\n", f"readonly blocked-write vi unexpectedly modified file: {saved!r}")

    exit_code, decoded, saved = helpers.run_vi_session(
        vi_path,
        "one\n",
        [b":q!\r"],
        final_keys=None,
        argv0="view",
    )
    require(exit_code == 0, f"view vi exited with status {exit_code}")
    require("[Readonly]" in decoded, "view vi missing readonly status")
    require(saved == "one\n", f"view vi unexpectedly modified file: {saved!r}")

    exit_code, decoded, saved = helpers.run_vi_session(
        vi_path,
        "one\n",
        [b"i", b"X", b"\x1b", b":next\r", b":q!\r"],
        final_keys=None,
        file_args=["buffer.txt", "two.txt"],
        extra_files={"two.txt": "two\n"},
    )
    require(exit_code == 0, f"visual modified-next vi exited with status {exit_code}")
    require("No write since last change (add ! to override)" in decoded,
            "visual modified-next vi missing diagnostic")
    require(saved == "one\n", f"visual modified-next vi unexpectedly modified file: {saved!r}")

    exit_code, decoded, saved = helpers.run_vi_session(
        vi_path,
        "one\n",
        [b"i", b"X", b"\x1b", b":next!\r", b":q!\r"],
        final_keys=None,
        file_args=["buffer.txt", "two.txt"],
        extra_files={"two.txt": "two\n"},
    )
    require(exit_code == 0, f"visual forced-next vi exited with status {exit_code}")
    require("/two.txt\" 1 lines" in decoded,
            "visual forced-next vi missing file-switch report")
    require(saved == "one\n", f"visual forced-next vi unexpectedly modified file: {saved!r}")

    exit_code, decoded, saved = helpers.run_vi_session(
        vi_path,
        "one\n",
        [b":q!\r"],
        final_keys=None,
        extra_args=["+next"],
        file_args=["buffer.txt", "two.txt"],
        extra_files={"two.txt": "two\n"},
    )
    require(exit_code == 0, f"visual +next startup vi exited with status {exit_code}")
    require("/two.txt" in decoded, "visual +next startup vi missing advanced file")
    require(saved == "one\n", f"visual +next startup vi unexpectedly modified file: {saved!r}")

    exit_code, decoded, saved = helpers.run_vi_session(
        vi_path,
        "one\ntwo\n",
        [b"Q", b"1d\n", b"visual\n", b"Q", b"q!\n"],
        final_keys=None,
    )
    require(exit_code == 0, f"vi/ex round-trip exited with status {exit_code}")
    require(decoded.count("\x1b[2J") >= 2,
            "vi/ex round-trip missing visual repaint after :visual")
    require(":1d" in decoded, "vi/ex round-trip missing ex-mode command echo")
    require(":visual" in decoded, "vi/ex round-trip missing visual handoff command echo")
    require(":q!" in decoded, "vi/ex round-trip missing final ex-mode quit prompt")
    require(saved == "one\ntwo\n", f"vi/ex round-trip unexpectedly saved changes: {saved!r}")

    exit_code, decoded, saved = helpers.run_vi_session(
        vi_path,
        "one\n",
        [b":ver\r", b":q!\r"],
        final_keys=None,
    )
    require(exit_code == 0, f"vi version status exited with status {exit_code}")
    require("Substrate vi v0.1" in decoded, "vi version missing status-line message")
    require(saved == "one\n", f"vi version unexpectedly modified file: {saved!r}")

    exit_code, decoded, saved = helpers.run_vi_session(
        vi_path,
        "one\ntwo\n",
        [b":set number\r", b":set wrapscan?\r", b":set nope\r", b":1delete\r", b":wq\r"],
        final_keys=None,
    )
    require(exit_code == 0, f"vi colon-command parity exited with status {exit_code}")
    require("      1 one" in decoded, "vi colon-command parity missing numbered render")
    require("wrapscan" in decoded, "vi colon-command parity missing option query output")
    require("Unknown option: nope" in decoded,
            "vi colon-command parity missing diagnostic output")
    require(saved == "two\n", f"vi colon-command parity saved wrong buffer: {saved!r}")

    exit_code, decoded, saved = helpers.run_vi_session(
        vi_path,
        "foo\n",
        [b":set number\"tail\r", b":1p\r", b":q!\r"],
        final_keys=None,
    )
    require(exit_code == 0, f"vi trailing-comment parsing exited with status {exit_code}")
    require("      1 foo" in decoded,
            "vi trailing-comment parsing missing numbered output")
    require(saved == "foo\n",
            f"vi trailing-comment parsing unexpectedly modified file: {saved!r}")

    exit_code, decoded, saved = helpers.run_vi_session(
        vi_path,
        "foo\n",
        [b":%s/o/bar|baz/\r", b":wq\r"],
        final_keys=None,
    )
    require(exit_code == 0, f"vi substitute-pipe parsing exited with status {exit_code}")
    require(saved == "fbar|bazo\n",
            f"vi substitute-pipe parsing saved wrong buffer: {saved!r}")

    exit_code, decoded, saved = helpers.run_vi_session(
        vi_path,
        "foo\nbar\n",
        [b":g/foo/s/o/|/\r", b":wq\r"],
        final_keys=None,
    )
    require(exit_code == 0, f"vi global-pipe parsing exited with status {exit_code}")
    require(saved == "f|o\nbar\n",
            f"vi global-pipe parsing saved wrong buffer: {saved!r}")

    exit_code, decoded, saved = helpers.run_vi_session(
        vi_path,
        "foo\nbar\n",
        [b":1s/foo/bar\r", b":wq\r"],
        final_keys=None,
    )
    require(exit_code == 0, f"vi malformed-substitute parsing exited with status {exit_code}")
    require(saved == "bar\nbar\n",
            f"vi malformed-substitute parsing saved wrong buffer: {saved!r}")

    exit_code, decoded, saved = helpers.run_vi_session(
        vi_path,
        "foo\nbar\n",
        [b":g/foo\r", b":wq\r"],
        final_keys=None,
    )
    require(exit_code == 0, f"vi malformed-global parsing exited with status {exit_code}")
    require(saved == "foo\nbar\n",
            f"vi malformed-global parsing saved wrong buffer: {saved!r}")

    exit_code, decoded, saved = helpers.run_vi_session(
        vi_path,
        "a/b/c\n",
        [b":%s/\\//:/g\r", b":wq\r"],
        final_keys=None,
    )
    require(exit_code == 0, f"vi escaped-pattern parsing exited with status {exit_code}")
    require(saved == "a:b:c\n",
            f"vi escaped-pattern parsing saved wrong buffer: {saved!r}")

    exit_code, decoded, saved = helpers.run_vi_session(
        vi_path,
        "foo\nbar\n",
        [b":'zp\r", b":q!\r"],
        final_keys=None,
    )
    require(exit_code == 0, f"vi bad-mark parsing exited with status {exit_code}")
    require("Bad address" in decoded,
            "vi bad-mark parsing missing diagnostic")
    require(saved == "foo\nbar\n",
            f"vi bad-mark parsing unexpectedly modified file: {saved!r}")

    exit_code, decoded, saved = helpers.run_vi_session(
        vi_path,
        "foo\nbar\n",
        [b":bogus\r", b":q!\r"],
        final_keys=None,
    )
    require(exit_code == 0, f"vi unknown-command handling exited with status {exit_code}")
    require("Unknown command" in decoded,
            "vi unknown-command handling missing diagnostic")
    require(saved == "foo\nbar\n",
            f"vi unknown-command handling unexpectedly modified file: {saved!r}")

    exit_code, decoded, saved = helpers.run_vi_session(
        vi_path,
        "",
        [b":r insert.txt\r", b":wq\r"],
        final_keys=None,
        extra_files={"insert.txt": "A\nB\n"},
    )
    require(exit_code == 0, f"vi empty-read handling exited with status {exit_code}")
    require(saved == "A\nB\n",
            f"vi empty-read handling saved wrong buffer: {saved!r}")

    exit_code, decoded, saved = helpers.run_vi_session(
        vi_path,
        "one\ntwo\nthree\n",
        [b":2r insert.txt\r", b":wq\r"],
        final_keys=None,
        extra_files={"insert.txt": "A\nB\n"},
    )
    require(exit_code == 0, f"vi addressed-read handling exited with status {exit_code}")
    require(saved == "one\ntwo\nA\nB\nthree\n",
            f"vi addressed-read handling saved wrong buffer: {saved!r}")

    exit_code, decoded, saved = helpers.run_vi_session(
        vi_path,
        "",
        [b":r !printf 'A\\nB\\n'\r", b":wq\r"],
        final_keys=None,
    )
    require(exit_code == 0, f"vi shell-read handling exited with status {exit_code}")
    require(saved == "A\nB\n",
            f"vi shell-read handling saved wrong buffer: {saved!r}")

    exit_code, decoded, saved = helpers.run_vi_session(
        vi_path,
        "one\ntwo\nthree\n",
        [b":2r !printf 'A\\nB\\n'\r", b":wq\r"],
        final_keys=None,
    )
    require(exit_code == 0, f"vi addressed shell-read handling exited with status {exit_code}")
    require(saved == "one\ntwo\nA\nB\nthree\n",
            f"vi addressed shell-read handling saved wrong buffer: {saved!r}")

    exit_code, decoded, saved = helpers.run_vi_session(
        vi_path,
        "one\ntwo\nthree\n",
        [b":2j\r", b":wq\r"],
        final_keys=None,
    )
    require(exit_code == 0, f"vi join-address defaults exited with status {exit_code}")
    require(saved == "one\ntwo three\n",
            f"vi join-address defaults saved wrong buffer: {saved!r}")

    exit_code, decoded, saved = helpers.run_vi_session(
        vi_path,
        "one\ntwo\nthree\n",
        [b":2p\r", b":1,?p\r", b":p\r", b":q!\r"],
        final_keys=None,
    )
    require(exit_code == 0, f"vi malformed-range handling exited with status {exit_code}")
    require(decoded.count("two") >= 2,
            "vi malformed-range handling missing preserved current-line output")
    require("Bad address" in decoded,
            "vi malformed-range handling missing diagnostic")
    require(saved == "one\ntwo\nthree\n",
            f"vi malformed-range handling unexpectedly modified file: {saved!r}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
