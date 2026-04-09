#!/usr/bin/env python3

import importlib.util
import fcntl
import os
import pty
import shutil
import signal
import struct
import subprocess
import sys
import tempfile
import termios


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


def interrupt_vi_session(helpers, vi_path, initial_text, key_steps, extra_args=None):
    temp_dir = tempfile.mkdtemp(prefix="exvi-")
    temp_path = os.path.join(temp_dir, "buffer.txt")
    with open(temp_path, "w", encoding="utf-8") as f:
        f.write(initial_text)

    pid, master_fd = pty.fork()
    if pid == 0:
        argv = [vi_path]
        if extra_args:
            argv.extend(extra_args)
        argv.append(temp_path)
        os.chdir(temp_dir)
        os.execv(vi_path, argv)

    fcntl.ioctl(master_fd, termios.TIOCSWINSZ, struct.pack("HHHH", 24, 80, 0, 0))
    output = helpers.read_some(master_fd, 0.4)
    for step in key_steps:
        os.write(master_fd, step)
        output += helpers.read_some(master_fd, 0.2)
    os.kill(pid, signal.SIGTERM)
    output += helpers.read_some(master_fd, 0.2)
    _, status = os.waitpid(pid, 0)
    os.close(master_fd)

    recover_path = temp_path + ".recover"
    recovered = None
    if os.path.exists(recover_path):
        with open(recover_path, "r", encoding="utf-8") as f:
            recovered = f.read()

    return (os.waitstatus_to_exitcode(status), output.decode("latin1", "replace"),
            recovered, temp_path, temp_dir)


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

    exit_code, decoded, recovered, path, temp_dir = interrupt_vi_session(
        helpers,
        vi_path,
        "one\n",
        [b"i", b"X", b"\x1b"],
    )
    try:
        require(exit_code in (-signal.SIGTERM, 1),
                f"interrupted vi status mismatch: {exit_code}")
        require(recovered == "Xone\n",
                f"interrupted vi recover contents mismatch: {recovered!r}")

        pid, master_fd = pty.fork()
        if pid == 0:
            os.chdir(os.path.dirname(path))
            os.execv(vi_path, [vi_path, "-r", path])
        fcntl.ioctl(master_fd, termios.TIOCSWINSZ, struct.pack("HHHH", 24, 80, 0, 0))
        recover_output = helpers.read_some(master_fd, 0.4)
        os.write(master_fd, b":q!\r")
        recover_output += helpers.read_some(master_fd, 0.3)
        _, status = os.waitpid(pid, 0)
        os.close(master_fd)
        require(os.waitstatus_to_exitcode(status) == 0,
                "interrupted vi -r status mismatch")
        require("recovered, 1 lines" in recover_output.decode("latin1", "replace"),
                "interrupted vi -r missing recovered status")
    finally:
        if os.path.exists(path + ".recover"):
            os.unlink(path + ".recover")
        shutil.rmtree(temp_dir)

    exit_code, decoded, recovered, path, temp_dir = interrupt_vi_session(
        helpers,
        vi_path,
        "one",
        [b"r", b"O"],
    )
    try:
        require(exit_code in (-signal.SIGTERM, 1),
                f"interrupted no-eol vi status mismatch: {exit_code}")
        require(recovered == "One",
                f"interrupted no-eol vi recover contents mismatch: {recovered!r}")
    finally:
        if os.path.exists(path + ".recover"):
            os.unlink(path + ".recover")
        shutil.rmtree(temp_dir)

    temp_dir = tempfile.mkdtemp(prefix="exvi-")
    path = os.path.join(temp_dir, "buffer.txt")
    try:
        with open(path, "w", encoding="utf-8") as f:
            f.write("one\n")
        with open(path + ".recover", "w", encoding="utf-8") as f:
            f.write("STALE\n")

        pid, master_fd = pty.fork()
        if pid == 0:
            os.chdir(temp_dir)
            os.execv(vi_path, [vi_path, path])

        fcntl.ioctl(master_fd, termios.TIOCSWINSZ, struct.pack("HHHH", 24, 80, 0, 0))
        output = helpers.read_some(master_fd, 0.4)
        os.write(master_fd, b":wq\r")
        output += helpers.read_some(master_fd, 0.3)
        _, status = os.waitpid(pid, 0)
        os.close(master_fd)
        require(os.waitstatus_to_exitcode(status) == 0,
                "vi stale-recover cleanup status mismatch")
        require(not os.path.exists(path + ".recover"),
                "vi successful write did not remove stale recover file")
    finally:
        if os.path.exists(path + ".recover"):
            os.unlink(path + ".recover")
        shutil.rmtree(temp_dir)

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

    env_path = shutil.which("env") or "/usr/bin/env"
    for label, locale_args in (
        ("c", ["LC_ALL=C", "LC_CTYPE=C", "LANG=C"]),
        ("posix", ["LC_ALL=POSIX", "LC_CTYPE=POSIX", "LANG=POSIX"]),
        ("latin1", ["LC_ALL=en_US.ISO-8859-1", "LC_CTYPE=en_US.ISO-8859-1",
            "LANG=en_US.ISO-8859-1"]),
    ):
        exit_code, decoded, saved = helpers.run_vi_session(
            env_path,
            "one\ntwo\n",
            [b"/", b"two\r", b"A", b"Z", b"\x1b"],
            extra_args=locale_args + [vi_path],
            argv0="env",
        )
        require(exit_code == 0, f"locale-fallback-{label} vi exited with status {exit_code}")
        require("line 2/2" in decoded,
                f"locale-fallback-{label} missing search status under fallback locale")
        require(saved == "one\ntwoZ\n",
                f"locale-fallback-{label} buffer mismatch: {saved!r}")

    exit_code, decoded, saved = helpers.run_vi_session(
        vi_path,
        "",
        [b":q!\r"],
        final_keys=None,
        extra_args=["-t", "two"],
        file_args=[],
        extra_files={
            "target.txt": "alpha\nbeta\n",
            "tags": "two\ttarget.txt\t2\n",
        },
    )
    require(exit_code == 0, f"vi -t startup exited with status {exit_code}")
    require("line 2/2" in decoded, "vi -t startup missing tag target status")
    require(saved == "", f"vi -t startup unexpectedly modified default scratch file: {saved!r}")

    exit_code, decoded, saved = helpers.run_vi_session(
        vi_path,
        "one\n",
        [b":!true\r", b":q!\r"],
        final_keys=None,
        extra_args=["-S"],
    )
    require(exit_code == 0, f"secure vi shell escape exited with status {exit_code}")
    require("Shell commands not allowed in secure mode" in decoded,
            "secure vi shell escape missing diagnostic")
    require(saved == "one\n", f"secure vi shell escape unexpectedly modified file: {saved!r}")

    exit_code, decoded, saved = helpers.run_vi_session(
        vi_path,
        "one\n",
        [b":r !printf 'A\\n'\r", b":q!\r"],
        final_keys=None,
        extra_args=["-S"],
    )
    require(exit_code == 0, f"secure vi shell read exited with status {exit_code}")
    require("Shell commands not allowed in secure mode" in decoded,
            "secure vi shell read missing diagnostic")
    require(saved == "one\n", f"secure vi shell read unexpectedly modified file: {saved!r}")

    exit_code, decoded, saved = helpers.run_vi_session(
        vi_path,
        "one\n",
        [b":w !cat\r", b":q!\r"],
        final_keys=None,
        extra_args=["-S"],
    )
    require(exit_code == 0, f"secure vi shell write exited with status {exit_code}")
    require("Shell commands not allowed in secure mode" in decoded,
            "secure vi shell write missing diagnostic")
    require(saved == "one\n", f"secure vi shell write unexpectedly modified file: {saved!r}")

    exit_code, decoded, saved = helpers.run_vi_session(
        vi_path,
        "one\n",
        [b":!true\r", b":q!\r"],
        final_keys=None,
        argv0="rvi",
    )
    require(exit_code == 0, f"restricted vi shell escape exited with status {exit_code}")
    require("Shell commands not allowed in restricted mode" in decoded,
            "restricted vi shell escape missing diagnostic")
    require(saved == "one\n", f"restricted vi shell escape unexpectedly modified file: {saved!r}")

    exit_code, decoded, saved = helpers.run_vi_session(
        vi_path,
        "one\n",
        [b":e two.txt\r", b":w two.txt\r", b":q!\r"],
        final_keys=None,
        argv0="rvi",
        extra_files={"two.txt": "two\n"},
    )
    require(exit_code == 0, f"restricted vi file-change status mismatch: {exit_code}")
    require("File changes not allowed in restricted mode" in decoded,
            "restricted vi file-change missing diagnostics")
    require(saved == "one\n", f"restricted vi file-change unexpectedly modified file: {saved!r}")

    exit_code, decoded, saved = helpers.run_vi_session(
        vi_path,
        "one\n",
        [b":next\r", b":q!\r"],
        final_keys=None,
        argv0="rvi",
        file_args=["buffer.txt", "two.txt"],
        extra_files={"two.txt": "two\n"},
    )
    require(exit_code == 0, f"restricted vi next status mismatch: {exit_code}")
    require("File changes not allowed in restricted mode" in decoded,
            "restricted vi next missing diagnostic")
    require(saved == "one\n", f"restricted vi next unexpectedly modified file: {saved!r}")

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
        [b":set number\r", b":set wrapscan?\r", b":set ignorecase?\r",
         b":set ignorecase\r", b":set ignorecase?\r", b":set nope\r",
         b":1delete\r", b":wq\r"],
        final_keys=None,
    )
    require(exit_code == 0, f"vi colon-command parity exited with status {exit_code}")
    require("      1 one" in decoded, "vi colon-command parity missing numbered render")
    require("wrapscan" in decoded, "vi colon-command parity missing option query output")
    require("noignorecase" in decoded, "vi colon-command parity missing initial ignorecase query")
    require("ignorecase" in decoded, "vi colon-command parity missing ignorecase query output")
    require("Unknown option: nope" in decoded,
            "vi colon-command parity missing diagnostic output")
    require(saved == "two\n", f"vi colon-command parity saved wrong buffer: {saved!r}")

    exit_code, decoded, saved = helpers.run_vi_session(
        vi_path,
        "one\ntwo\nthree\n",
        [b":1delete\r", b"p", b":wq\r"],
        final_keys=None,
    )
    require(exit_code == 0, f"vi colon delete register carry exited with status {exit_code}")
    require(saved == "two\none\nthree\n",
            f"vi colon delete register carry saved wrong buffer: {saved!r}")

    exit_code, decoded, saved = helpers.run_vi_session(
        vi_path,
        "alpha\nA a\n",
        [b":set ignorecase\r", b"/ALPHA\r", b"r", b"X", b":wq\r"],
        final_keys=None,
    )
    require(exit_code == 0, f"vi ignorecase search exited with status {exit_code}")
    require(saved == "Xlpha\nA a\n",
            f"vi ignorecase search saved wrong buffer: {saved!r}")

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
        "foo\nbar\n",
        [b":tag\r", b":q!\r"],
        final_keys=None,
    )
    require(exit_code == 0, f"vi missing-tag-operand handling exited with status {exit_code}")
    require("Usage: tag <name>" in decoded,
            "vi missing-tag-operand handling missing diagnostic")
    require(saved == "foo\nbar\n",
            f"vi missing-tag-operand handling unexpectedly modified file: {saved!r}")

    exit_code, decoded, saved = helpers.run_vi_session(
        vi_path,
        "foo\nbar\n",
        [b":copy\r", b":q!\r"],
        final_keys=None,
    )
    require(exit_code == 0, f"vi missing-copy-destination handling exited with status {exit_code}")
    require("Destination required" in decoded,
            "vi missing-copy-destination handling missing diagnostic")
    require(saved == "foo\nbar\n",
            f"vi missing-copy-destination handling unexpectedly modified file: {saved!r}")

    exit_code, decoded, saved = helpers.run_vi_session(
        vi_path,
        "foo\nbar\nbaz\n",
        [b":1,2move1\r", b":q!\r"],
        final_keys=None,
    )
    require(exit_code == 0, f"vi bad-move-destination handling exited with status {exit_code}")
    require("Destination not outside move range" in decoded,
            "vi bad-move-destination handling missing diagnostic")
    require(saved == "foo\nbar\nbaz\n",
            f"vi bad-move-destination handling unexpectedly modified file: {saved!r}")

    exit_code, decoded, saved = helpers.run_vi_session(
        vi_path,
        "foo\nbar\n",
        [b"i", b"X", b"\x1b", b":q\r", b":q!\r"],
        final_keys=None,
    )
    require(exit_code == 0, f"vi modified-quit handling exited with status {exit_code}")
    require("No write since last change (add ! to override)" in decoded,
            "vi modified-quit handling missing diagnostic")
    require(saved == "foo\nbar\n",
            f"vi modified-quit handling unexpectedly modified file: {saved!r}")

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
        [b":set tags=alt.tags\r", b":tag one\r", b":q!\r"],
        final_keys=None,
        extra_files={"alt.tags": "one\tbuffer.txt\t3\n"},
    )
    require(exit_code == 0, f"vi tags option handling exited with status {exit_code}")
    require("line 3/3" in decoded, "vi tags option handling missing tag jump status")
    require(saved == "one\ntwo\nthree\n",
            f"vi tags option handling unexpectedly modified file: {saved!r}")

    exit_code, decoded, saved = helpers.run_vi_session(
        vi_path,
        "one\ntwo\nthree\n",
        [b":tag one\r", b":tags\r", b":q!\r"],
        final_keys=None,
        extra_files={"tags": "one\tbuffer.txt\t3\n"},
    )
    require(exit_code == 0, f"vi tags reporting exited with status {exit_code}")
    require("> one buffer.txt:3" in decoded,
            "vi tags reporting missing current tag name")
    require(saved == "one\ntwo\nthree\n",
            f"vi tags reporting unexpectedly modified file: {saved!r}")

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

    proc = subprocess.run([vi_path, "--version"], input=b"",
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    require(proc.returncode == 1, f"vi --version status mismatch: {proc.returncode}")
    require(proc.stdout == b"", f"vi --version stdout mismatch: {proc.stdout!r}")
    require("Usage:" in proc.stderr.decode("latin1", "replace"),
            f"vi --version stderr mismatch: {proc.stderr!r}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
