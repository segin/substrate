#!/usr/bin/env python3

import fcntl
import os
import pty
import select
import struct
import sys
import tempfile
import time
import termios


def read_some(master_fd, timeout):
    out = bytearray()
    end = time.time() + timeout
    while time.time() < end:
        ready, _, _ = select.select([master_fd], [], [], 0.05)
        if not ready:
            continue
        try:
            chunk = os.read(master_fd, 4096)
        except OSError:
            break
        if not chunk:
            break
        out.extend(chunk)
    return bytes(out)


def require(cond, msg):
    if not cond:
        print(f"FAIL: {msg}", file=sys.stderr)
        raise SystemExit(1)


def send_keys(master_fd, output, data, timeout=0.2):
    os.write(master_fd, data)
    output += read_some(master_fd, timeout)
    return output


def run_vi_session(vi_path, initial_text, key_steps, final_timeout=0.3, rows=24,
                   cols=80, final_keys=b":wq\r", extra_files=None,
                   extra_args=None, argv0=None, file_args=None,
                   session_timeout=15.0):
    with tempfile.TemporaryDirectory(prefix="exvi-") as temp_dir:
        temp_path = os.path.join(temp_dir, "buffer.txt")
        with open(temp_path, "w", encoding="utf-8") as f:
            f.write(initial_text)

        if extra_files:
            for relpath, contents in extra_files.items():
                full_path = os.path.join(temp_dir, relpath)
                os.makedirs(os.path.dirname(full_path), exist_ok=True)
                with open(full_path, "w", encoding="utf-8") as f:
                    f.write(contents)

        pid, master_fd = pty.fork()
        if pid == 0:
            argv = [argv0 or vi_path]
            if extra_args:
                argv.extend(extra_args)
            if file_args is not None:
                for arg in file_args:
                    if os.path.isabs(arg):
                        argv.append(arg)
                    else:
                        argv.append(os.path.join(temp_dir, arg))
            else:
                argv.append(temp_path)
            os.chdir(temp_dir)
            os.execv(vi_path, argv)

        fcntl.ioctl(master_fd, termios.TIOCSWINSZ, struct.pack("HHHH", rows, cols, 0, 0))
        output = read_some(master_fd, 0.4)
        for step in key_steps:
            if isinstance(step, tuple) and step and step[0] == "winsize":
                _, step_rows, step_cols, *rest = step
                step_timeout = rest[0] if rest else 0.3

                fcntl.ioctl(master_fd, termios.TIOCSWINSZ,
                            struct.pack("HHHH", step_rows, step_cols, 0, 0))
                output += read_some(master_fd, step_timeout)
                continue
            output = send_keys(master_fd, output, step)
        if final_keys is not None:
            output = send_keys(master_fd, output, final_keys[:1])
            output = send_keys(master_fd, output, final_keys[1:], final_timeout)
        else:
            output += read_some(master_fd, final_timeout)

        deadline = time.time() + session_timeout
        status = None
        while time.time() < deadline:
            pid_done, wait_status = os.waitpid(pid, os.WNOHANG)
            if pid_done == pid:
                status = wait_status
                break
            output += read_some(master_fd, 0.05)
            time.sleep(0.01)
        if status is None:
            os.kill(pid, 9)
            _, status = os.waitpid(pid, 0)
            raise RuntimeError(
                f"vi session timed out after {session_timeout:.1f}s; "
                f"partial output tail={output.decode('latin1', 'replace')[-200:]!r}"
            )
        os.close(master_fd)

        with open(temp_path, "r", encoding="utf-8") as f:
            saved = f.read()

        return os.waitstatus_to_exitcode(status), output.decode("latin1", "replace"), saved


def main():
    if len(sys.argv) != 2:
        print(f"usage: {sys.argv[0]} /path/to/vi", file=sys.stderr)
        return 2

    vi_path = sys.argv[1]
    exit_code, decoded, saved = run_vi_session(vi_path, "one\ntwo\nthree\ntwo again\nfive\n", [
        b"G", b"g", b"g", b"/", b"two\r", b"n", b"N", b"?", b"one\r", b"g", b"g",
        b"i", b"X", b"\x1b", b"j", b"x", b"j", b"a", b"!", b"\x1b", b"u",
        b"O", b"T", b"o", b"p", b"\x1b", b"o", b"t", b"a", b"i", b"l", b"\x1b",
        b"g", b"g", b"$", b"a", b"-", b"s", b"p", b"l", b"i", b"t", b"\r",
        b"l", b"i", b"n", b"e", b"\x1b", b"w", b"e", b"b", b"y", b"y", b"p",
        b"P", b"d", b"d", b"/", b"wo\r", b"c", b"w", b"T", b"W", b"O", b"\x1b",
        b"/", b"two again\r", b"w", b"e", b"b", b"D", b"/", b"tail\r", b"C",
        b"T", b"A", b"I", b"L", b"1", b"\x1b", b"/", b"three\r", b"s", b"T",
        b"H", b"\x1b", b"/", b"two \r", b"R", b"T", b"w", b"o", b"!", b"\x1b",
        b"/", b"five\r", b"r", b"F", b"/", b"THhree\r", b"A", b"!", b"\x1b",
        b"/", b"Two!\r", b"I", b">", b"\x1b", b"/", b"Five\r", b"S", b"F",
        b"i", b"n", b"a", b"l", b"\x1b", b"/", b"Xone\r", b"J", b"/", b"tail\r",
        b"d", b"w", b"/", b"Final\r", b"c", b"c", b"D", b"o", b"n", b"e",
        b"\x1b", b"/", b"Xone\r", b"3", b"J", b"3", b"G", b"2", b"x", b".",
        b"g", b"g", b"l", b"l", b"^", b"\x06", b"H", b"M", b"L",
    ])
    require(exit_code == 0, f"vi exited with status {exit_code}")
    require("\x1b[" in decoded, "missing ANSI screen output")
    require("one" in decoded, "missing initial buffer content")
    require("line 5/5" in decoded, "missing G navigation status")
    require("line 1/5" in decoded, "missing gg navigation status")
    require("line 2/5" in decoded, "missing forward search status")
    require("line 4/5" in decoded, "missing repeat search status")
    require("line 3/6" in decoded, "missing count-based G navigation status")
    require("line 6/6" in decoded, "missing page-scroll status")
    require("-- INSERT --" in decoded, "missing insert mode status")
    require("-- REPLACE --" in decoded, "missing replace mode status")
    require(":wq" in decoded, "missing ex command prompt rendering")
    require("\r\n\x1b[K~" in decoded, "missing CRLF ladder rendering for screen filler")
    require(saved == "Top-split\nline\n1\n\nXone TWO THhree! >Two!\nDone\n",
            f"unexpected saved buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\ntwo\nthree\n",
        [b"/", b"junk", b"\x15", b"two\r", b"r", b"Z"],
    )
    require(exit_code == 0, f"search-prompt-ctrl-u vi exited with status {exit_code}")
    require("line 2/3" in decoded, "missing forward-search status after Ctrl-U prompt edit")
    require(saved == "one\nZwo\nthree\n",
            f"unexpected search-prompt-ctrl-u buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\ntwo\nthree\n",
        [b"G", b"?", b"junk", b"\x17", b"two\r", b"r", b"Z"],
    )
    require(exit_code == 0, f"search-prompt-ctrl-w vi exited with status {exit_code}")
    require("line 2/3" in decoded, "missing backward-search status after Ctrl-W prompt edit")
    require(saved == "one\nZwo\nthree\n",
            f"unexpected search-prompt-ctrl-w buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\ntwo\nthree\n",
        [b"/", b"two", b"\x1b", b"r", b"Z"],
    )
    require(exit_code == 0, f"search-cancel vi exited with status {exit_code}")
    require(saved == "Zne\ntwo\nthree\n",
            f"unexpected search-cancel buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\ntwo\nthree\n",
        [b"/", b"missing\r", b"r", b"Z"],
    )
    require(exit_code == 0, f"search-fail vi exited with status {exit_code}")
    require("Pattern not found: missing" in decoded, "missing failed-search status message")
    require(saved == "Zne\ntwo\nthree\n",
            f"unexpected search-fail buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one two three\nalpha two beta\n",
        [b"/", b"two\r", b"r", b"Z", b"n", b"r", b"Y"],
    )
    require(exit_code == 0, f"search-column vi exited with status {exit_code}")
    require("line 1/2" in decoded, "missing same-line search status")
    require("line 2/2" in decoded, "missing repeated search status")
    require(saved == "one Zwo three\nalpha Ywo beta\n",
            f"unexpected search-column buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\ntwo\nthree\n",
        [b"\x1bOB", b"\x1bOC", b"r", b"Z", b"\x1bOA", b"\x1bOH", b"r", b"Q",
         b"\x1bOF", b"r", b"R"],
    )
    require(exit_code == 0, f"vt100-ss3-normal vi exited with status {exit_code}")
    require("\x1b[?1h\x1b=" in decoded, "missing VT100 application-keypad enter sequence")
    require("\x1b[?1l\x1b>" in decoded, "missing VT100 application-keypad restore sequence")
    require(saved == "QnR\ntZo\nthree\n",
            f"unexpected vt100-ss3-normal buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "abc\n",
        [b"i", b"X", b"\x1bOD", b"Y", b"\x1bOH", b"Z", b"\x1b"],
    )
    require(exit_code == 0, f"vt100-ss3-insert vi exited with status {exit_code}")
    require(saved == "ZYXabc\n",
            f"unexpected vt100-ss3-insert buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\ntwo\none again\n",
        [b"/", b"one\r", b"n", b"r", b"Z"],
    )
    require(exit_code == 0, f"search-wrap vi exited with status {exit_code}")
    require("line 3/3" in decoded, "missing initial wrapped-search target status")
    require("line 1/3" in decoded, "missing wrapped n repeat status")
    require(saved == "Zne\ntwo\none again\n",
            f"unexpected search-wrap buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\ntwo\none again\n",
        [b":set nowrapscan\r", b"/", b"one\r", b"n", b"r", b"Z"],
    )
    require(exit_code == 0, f"search-nowrap vi exited with status {exit_code}")
    require("line 3/3" in decoded, "missing nowrap initial search status")
    require("Pattern not found: one" in decoded, "missing nowrap n failure status")
    require(saved == "one\ntwo\nZne again\n",
            f"unexpected search-nowrap buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "alpha\nbeta\ngamma\n",
        [b"G", b":set nowrapscan\r", b"d", b"/", b"a", b"l", b"p", b"h", b"a", b"\r",
         b"r", b"Z"],
    )
    require(exit_code == 0, f"d/search-nowrap vi exited with status {exit_code}")
    require("Pattern not found: alpha" in decoded,
            "missing nowrap operator-search failure status")
    require(saved == "alpha\nbeta\nZamma\n",
            f"unexpected d/search-nowrap buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "word\nmiddle\nword\n",
        [b"G", b":set nowrapscan\r", b"*", b"r", b"Z"],
    )
    require(exit_code == 0, f"star-nowrap vi exited with status {exit_code}")
    require("Pattern not found: word" in decoded, "missing nowrap * failure status")
    require(saved == "word\nmiddle\nZord\n",
            f"unexpected star-nowrap buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "word\nmiddle\nword\n",
        [b":set nowrapscan\r", b"#", b"r", b"Z"],
    )
    require(exit_code == 0, f"hash-nowrap vi exited with status {exit_code}")
    require("Pattern not found: word" in decoded, "missing nowrap # failure status")
    require(saved == "Zord\nmiddle\nword\n",
            f"unexpected hash-nowrap buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\n",
        [b":ver\r", b":set tabstop?\r", b":\x1b[A\x1b[A\x1b[B\r", b":q!\r"],
        final_keys=None,
    )
    require(exit_code == 0, f"command-history vi exited with status {exit_code}")
    require(decoded.count("tabstop=8") >= 2, "missing command-history prompt recall")
    require(saved == "one\n", f"unexpected command-history buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\n",
        [b":set noshowmode\r", b"i", b"X", b"\x1b"],
        final_keys=b":q!\r",
    )
    require(exit_code == 0, f"noshowmode vi exited with status {exit_code}")
    require("-- INSERT --" not in decoded, "noshowmode still showed insert status")
    require(saved == "one\n", f"unexpected noshowmode buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\n",
        [b":set noshowmode\r", b":set showmode\r", b"i", b"X", b"\x1b"],
        final_keys=b":q!\r",
    )
    require(exit_code == 0, f"showmode vi exited with status {exit_code}")
    require("-- INSERT --" in decoded, "showmode did not restore insert status")
    require(saved == "one\n", f"unexpected showmode buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "    one\n",
        [b":set autoindent\r", b"o", b"a", b"\x1b"],
    )
    require(exit_code == 0, f"autoindent-open vi exited with status {exit_code}")
    require(saved == "    one\n    a\n",
            f"unexpected autoindent-open buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "    one\n",
        [b":set autoindent\r", b"A", b"\r", b"x", b"\x1b"],
    )
    require(exit_code == 0, f"autoindent-split vi exited with status {exit_code}")
    require(saved == "    one\n    x\n",
            f"unexpected autoindent-split buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "    one\n",
        [b":set autoindent\r", b":set noautoindent\r", b"o", b"a", b"\x1b"],
    )
    require(exit_code == 0, f"noautoindent vi exited with status {exit_code}")
    require(saved == "    one\na\n",
            f"unexpected noautoindent buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "1\n2\n3\n4\n5\n6\n7\n8\n",
        [b":set scroll=3\r", b"\x04", b"r", b"Z"],
    )
    require(exit_code == 0, f"scroll-option vi exited with status {exit_code}")
    require("line 4/8" in decoded, "missing scroll-option target status")
    require(saved == "1\n2\n3\nZ\n5\n6\n7\n8\n",
            f"unexpected scroll-option buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\ntwo\nthree\ntwo\n",
        [b"/two\r", b"/\x1b[A\r", b"?\x1b[A\r", b"r", b"Z"],
    )
    require(exit_code == 0, f"search-history vi exited with status {exit_code}")
    require("line 4/4" in decoded, "missing forward search-history repeat status")
    require(saved == "one\nZwo\nthree\ntwo\n",
            f"unexpected search-history buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(vi_path, "alpha beta gamma\nsecond line here\n", [
        b"w",
        b"d", b"e",
        b"w",
        b"c", b"$", b"D", b"O", b"N", b"E", b"\x1b",
        b"j",
        b"0", b"w",
        b"d", b"$",
    ])
    require(exit_code == 0, f"operator vi exited with status {exit_code}")
    require("-- INSERT --" in decoded, "missing operator insert status")
    require("line 2/2" in decoded, "missing operator second-line status")
    require(saved == "alpha  DONE\nsecond \n",
            f"unexpected operator-motion buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "alpha,beta   gamma delta\n",
        [b"E", b"r", b"4", b"0", b"W", b"r", b"1", b"$", b"B", b"r", b"3"],
    )
    require(exit_code == 0, f"bigword-motion vi exited with status {exit_code}")
    require("line 1/1" in decoded, "missing bigword-motion status")
    require(saved == "alpha,bet4   1amma 3elta\n",
            f"unexpected bigword-motion buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "alpha,beta   gamma\n",
        [b"d", b"W"],
    )
    require(exit_code == 0, f"bigword-operator vi exited with status {exit_code}")
    require(saved == "gamma\n",
            f"unexpected bigword-operator buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one two three\n",
        [b"d", b"w"],
    )
    require(exit_code == 0, f"dw vi exited with status {exit_code}")
    require(saved == "two three\n",
            f"unexpected dw buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one two three\n",
        [b"y", b"w", b"P"],
    )
    require(exit_code == 0, f"yw vi exited with status {exit_code}")
    require(saved == "one one two three\n",
            f"unexpected yw buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one,two   three\n",
        [b"y", b"W", b"P"],
    )
    require(exit_code == 0, f"yW vi exited with status {exit_code}")
    require(saved == "one,two   one,two   three\n",
            f"unexpected yW buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one two three\n",
        [b"d", b"e"],
    )
    require(exit_code == 0, f"de vi exited with status {exit_code}")
    require(saved == " two three\n",
            f"unexpected de buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one two three\n",
        [b"y", b"e", b"P"],
    )
    require(exit_code == 0, f"ye vi exited with status {exit_code}")
    require(saved == "oneone two three\n",
            f"unexpected ye buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one two three\n",
        [b"c", b"e", b"X", b"\x1b"],
    )
    require(exit_code == 0, f"ce vi exited with status {exit_code}")
    require("-- INSERT --" in decoded, "missing ce insert status")
    require(saved == "X two three\n",
            f"unexpected ce buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one,two   three\n",
        [b"d", b"E"],
    )
    require(exit_code == 0, f"dE vi exited with status {exit_code}")
    require(saved == "   three\n",
            f"unexpected dE buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one,two   three\n",
        [b"y", b"E", b"P"],
    )
    require(exit_code == 0, f"yE vi exited with status {exit_code}")
    require(saved == "one,twoone,two   three\n",
            f"unexpected yE buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one,two   three\n",
        [b"c", b"E", b"X", b"\x1b"],
    )
    require(exit_code == 0, f"cE vi exited with status {exit_code}")
    require("-- INSERT --" in decoded, "missing cE insert status")
    require(saved == "X   three\n",
            f"unexpected cE buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\ntwo\nthree\n",
        [b'"', b'a', b'y', b'y', b'j', b'"', b'a', b'p'],
    )
    require(exit_code == 0, f"named-yank-put vi exited with status {exit_code}")
    require(saved == "one\ntwo\none\nthree\n",
            f"unexpected named-yank-put buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\ntwo\nthree\n",
        [b'j', b'"', b'b', b'd', b'd', b'g', b'g', b'"', b'b', b'P'],
    )
    require(exit_code == 0, f"named-delete-put vi exited with status {exit_code}")
    require(saved == "two\none\nthree\n",
            f"unexpected named-delete-put buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\ntwo\n",
        [b'"', b'a', b'S', b'X', b'\x1b', b'"', b'a', b'P'],
    )
    require(exit_code == 0, f"named-substitute-put vi exited with status {exit_code}")
    require(saved == "one\nX\ntwo\n",
            f"unexpected named-substitute-put buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "alpha beta\n",
        [b'"', b'a', b'c', b'w', b'Z', b'\x1b', b'0', b'"', b'a', b'p'],
    )
    require(exit_code == 0, f"named-change-put vi exited with status {exit_code}")
    require(saved == "Zalpha beta\n",
            f"unexpected named-change-put buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\n",
        [b"i", b"X", b"\x1b", b":wq\r", b":q!\r"],
        final_keys=None,
        extra_args=["-R"],
    )
    require(exit_code == 0, f"readonly vi exited with status {exit_code}")
    require("[Readonly]" in decoded, "missing readonly status")
    require(saved == "one\n",
            f"unexpected readonly vi buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\n",
        [b":q!\r"],
        final_keys=None,
        argv0="view",
    )
    require(exit_code == 0, f"view-mode vi exited with status {exit_code}")
    require("[Readonly]" in decoded, "missing view readonly status")
    require(saved == "one\n",
            f"unexpected view-mode buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\n",
        [b"i", b"X", b"\x1b", b":wq!\r"],
        final_keys=None,
        extra_args=["-R"],
    )
    require(exit_code == 0, f"readonly force-write vi exited with status {exit_code}")
    require(saved == "Xone\n",
            f"unexpected readonly force-write buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\ntwo\nthree\n",
        [b"Y", b"P"],
    )
    require(exit_code == 0, f"Y vi exited with status {exit_code}")
    require(saved == "one\none\ntwo\nthree\n",
            f"unexpected Y buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\ntwo\nthree\n",
        [b"2", b"Y", b"P"],
    )
    require(exit_code == 0, f"2Y vi exited with status {exit_code}")
    require(saved == "one\ntwo\none\ntwo\nthree\n",
            f"unexpected 2Y buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\n",
        [b":args\r", b":next\r", b":prev\r", b":rewind\r"],
        final_keys=b":q!\r",
        extra_files={"two.txt": "two\n"},
        file_args=["buffer.txt", "two.txt"],
    )
    require(exit_code == 0, f"visual args/next/prev/rewind vi exited with status {exit_code}")
    require("[/tmp/exvi-" in decoded and "/buffer.txt] /tmp/exvi-" in decoded and "/two.txt " in decoded,
            "missing visual :args output")
    require("\"/tmp/exvi-" in decoded and "/two.txt\" 1 lines" in decoded,
            "missing visual :next file switch report")
    require(decoded.count("/buffer.txt\" 1 lines") >= 1,
            "missing visual rewind/prev return report")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\ntwo\nthree\n",
        [b"x"],
        extra_args=["+2"],
    )
    require(exit_code == 0, f"visual +cmd startup vi exited with status {exit_code}")
    require(saved == "one\nwo\nthree\n",
            f"unexpected visual +cmd startup buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\ntwo\nthree\n",
        [b"x"],
        extra_args=["-c", "2"],
    )
    require(exit_code == 0, f"visual -c startup vi exited with status {exit_code}")
    require(saved == "one\nwo\nthree\n",
            f"unexpected visual -c startup buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "a\n\tb\n\tc\n",
        [b">", b">"],
    )
    require(exit_code == 0, f">> vi exited with status {exit_code}")
    require(saved == "\ta\n\tb\n\tc\n",
            f"unexpected >> buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "a\n\tb\n\tc\n",
        [b"2", b">", b">"],
    )
    require(exit_code == 0, f"2>> vi exited with status {exit_code}")
    require(saved == "\ta\n\t\tb\n\tc\n",
            f"unexpected 2>> buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "a\n\tb\n\tc\n",
        [b"<", b"<"],
    )
    require(exit_code == 0, f"<< vi exited with status {exit_code}")
    require(saved == "a\n\tb\n\tc\n",
            f"unexpected << buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "a\n\tb\n\tc\n",
        [b"2", b"<", b"<"],
    )
    require(exit_code == 0, f"2<< vi exited with status {exit_code}")
    require(saved == "a\nb\n\tc\n",
            f"unexpected 2<< buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "a\nb\nc\n",
        [b">", b"j"],
    )
    require(exit_code == 0, f">j vi exited with status {exit_code}")
    require(saved == "\ta\n\tb\nc\n",
            f"unexpected >j buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "\ta\n\tb\n\tc\n",
        [b"<", b"G"],
    )
    require(exit_code == 0, f"<G vi exited with status {exit_code}")
    require(saved == "a\nb\nc\n",
            f"unexpected <G buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "a\nb\nc\n",
        [b"G", b">", b"g", b"g"],
    )
    require(exit_code == 0, f">gg vi exited with status {exit_code}")
    require(saved == "\ta\n\tb\n\tc\n",
            f"unexpected >gg buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one two\nthree four\n",
        [b">", b"w"],
    )
    require(exit_code == 0, f">w vi exited with status {exit_code}")
    require(saved == "\tone two\nthree four\n",
            f"unexpected >w buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one,two\nthree four\n",
        [b">", b"W"],
    )
    require(exit_code == 0, f">W vi exited with status {exit_code}")
    require(saved == "\tone,two\nthree four\n",
            f"unexpected >W buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one two\nthree four\n",
        [b">", b"E"],
    )
    require(exit_code == 0, f">E vi exited with status {exit_code}")
    require(saved == "\tone two\nthree four\n",
            f"unexpected >E buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one two\nthree four\n",
        [b">", b"e"],
    )
    require(exit_code == 0, f">e vi exited with status {exit_code}")
    require(saved == "\tone two\nthree four\n",
            f"unexpected >e buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "\tone two\n\tthree four\n",
        [b"<", b"w"],
    )
    require(exit_code == 0, f"<w vi exited with status {exit_code}")
    require(saved == "one two\n\tthree four\n",
            f"unexpected <w buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "\tone,two\n\tthree four\n",
        [b"<", b"W"],
    )
    require(exit_code == 0, f"<W vi exited with status {exit_code}")
    require(saved == "one,two\n\tthree four\n",
            f"unexpected <W buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "\tone two\n\tthree four\n",
        [b"<", b"e"],
    )
    require(exit_code == 0, f"<e vi exited with status {exit_code}")
    require(saved == "one two\n\tthree four\n",
            f"unexpected <e buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "\tone,two\n\tthree four\n",
        [b"<", b"E"],
    )
    require(exit_code == 0, f"<E vi exited with status {exit_code}")
    require(saved == "one,two\n\tthree four\n",
            f"unexpected <E buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "\tone\n\ttwo\nthree\n",
        [b"G", b"0", b"<", b"b"],
    )
    require(exit_code == 0, f"<b vi exited with status {exit_code}")
    require(saved == "\tone\ntwo\nthree\n",
            f"unexpected <b buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "\tone,\n\ttwo\nthree\n",
        [b"G", b"<", b"B"],
    )
    require(exit_code == 0, f"<B vi exited with status {exit_code}")
    require(saved == "\tone,\ntwo\nthree\n",
            f"unexpected <B buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "\tone,\n\ttwo\nthree\n",
        [b"G", b"<", b"g", b"E"],
    )
    require(exit_code == 0, f"<gE vi exited with status {exit_code}")
    require(saved == "\tone,\ntwo\nthree\n",
            f"unexpected <gE buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one two\nthree four\n",
        [b"G", b">", b"b"],
    )
    require(exit_code == 0, f">b vi exited with status {exit_code}")
    require(saved == "\tone two\nthree four\n",
            f"unexpected >b buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one,two\nthree,four\n",
        [b"G", b">", b"B"],
    )
    require(exit_code == 0, f">B vi exited with status {exit_code}")
    require(saved == "\tone,two\nthree,four\n",
            f"unexpected >B buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one two\nthree four\n",
        [b"G", b">", b"g", b"e"],
    )
    require(exit_code == 0, f">ge vi exited with status {exit_code}")
    require(saved == "\tone two\n\tthree four\n",
            f"unexpected >ge buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one,two\nthree,four\n",
        [b"G", b">", b"g", b"E"],
    )
    require(exit_code == 0, f">gE vi exited with status {exit_code}")
    require(saved == "\tone,two\n\tthree,four\n",
            f"unexpected >gE buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "\tone two\n\tthree four\n",
        [b"G", b"<", b"g", b"e"],
    )
    require(exit_code == 0, f"<ge vi exited with status {exit_code}")
    require(saved == "one two\nthree four\n",
            f"unexpected <ge buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one two\nthree four\n",
        [b">", b"g", b"e"],
    )
    require(exit_code == 0, f">ge-start vi exited with status {exit_code}")
    require(saved == "one two\nthree four\n",
            f"unexpected >ge-start buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one,two\nthree four\n",
        [b">", b"g", b"E"],
    )
    require(exit_code == 0, f">gE-start vi exited with status {exit_code}")
    require(saved == "one,two\nthree four\n",
            f"unexpected >gE-start buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "abcd\n",
        [b'x', b'p'],
    )
    require(exit_code == 0, f"char-put vi exited with status {exit_code}")
    require(saved == "bacd\n",
            f"unexpected char-put buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "abcd\n",
        [b"x", b"u", b"\x12"],
    )
    require(exit_code == 0, f"redo vi exited with status {exit_code}")
    require(saved == "bcd\n",
            f"unexpected redo buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "alpha beta\n",
        [b'd', b'w', b'P'],
    )
    require(exit_code == 0, f"char-operator-put vi exited with status {exit_code}")
    require(saved == "alpha beta\n",
            f"unexpected char-operator-put buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "alpha beta\n",
        [b'y', b'w', b'$', b'p'],
    )
    require(exit_code == 0, f"char-yank-put vi exited with status {exit_code}")
    require(saved == "alpha betaalpha \n",
            f"unexpected char-yank-put buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "abcd\n",
        [b'"', b'a', b'x', b'l', b'"', b'a', b'p'],
    )
    require(exit_code == 0, f"named-char-put vi exited with status {exit_code}")
    require(saved == "bcad\n",
            f"unexpected named-char-put buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "abcd\n",
        [b'"', b'a', b'y', b'w', b'$', b'"', b'a', b'p'],
    )
    require(exit_code == 0, f"named-char-yank vi exited with status {exit_code}")
    require(saved == "abcdabcd\n",
            f"unexpected named-char-yank buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "abcd\n",
        [b'y', b'l', b'$', b'p'],
    )
    require(exit_code == 0, f"char-hl-yank vi exited with status {exit_code}")
    require(saved == "abcda\n",
            f"unexpected char-hl-yank buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "abcd\n",
        [b'l', b'l', b'd', b'h'],
    )
    require(exit_code == 0, f"char-hl-delete vi exited with status {exit_code}")
    require(saved == "acd\n",
            f"unexpected char-hl-delete buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "abc\n",
        [b"i", b"X", b"\t", b"Y", b"\x1b"],
    )
    require(exit_code == 0, f"insert-tab vi exited with status {exit_code}")
    require("-- INSERT --" in decoded, "missing insert-tab insert status")
    require(saved == "X\tYabc\n",
            f"unexpected insert-tab buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "abc\n",
        [b"i", b"\x14", b"X", b"\x1b"],
    )
    require(exit_code == 0, f"insert-ctrl-t vi exited with status {exit_code}")
    require("-- INSERT --" in decoded, "missing insert-ctrl-t insert status")
    require(saved == "\tXabc\n",
            f"unexpected insert-ctrl-t buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "abc\n",
        [b"i", b"\t", b"\x04", b"X", b"\x1b"],
    )
    require(exit_code == 0, f"insert-ctrl-d vi exited with status {exit_code}")
    require("-- INSERT --" in decoded, "missing insert-ctrl-d insert status")
    require(saved == "Xabc\n",
            f"unexpected insert-ctrl-d buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "alpha beta\n",
        [b"i", b"1", b"2", b"3", b"\x15", b"X", b"\x1b"],
    )
    require(exit_code == 0, f"insert-ctrl-u vi exited with status {exit_code}")
    require("-- INSERT --" in decoded, "missing insert-ctrl-u insert status")
    require(saved == "Xalpha beta\n",
            f"unexpected insert-ctrl-u buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "abc\ndef\nghi\n",
        [b"j", b"0", b"i", b"\x19", b"\x1b"],
    )
    require(exit_code == 0, f"insert-ctrl-y vi exited with status {exit_code}")
    require("-- INSERT --" in decoded, "missing insert-ctrl-y insert status")
    require(saved == "abc\nadef\nghi\n",
            f"unexpected insert-ctrl-y buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "abc\ndef\nghi\n",
        [b"j", b"0", b"i", b"\x05", b"\x1b"],
    )
    require(exit_code == 0, f"insert-ctrl-e vi exited with status {exit_code}")
    require("-- INSERT --" in decoded, "missing insert-ctrl-e insert status")
    require(saved == "abc\ngdef\nghi\n",
            f"unexpected insert-ctrl-e buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "abc\ndef\nghi\n",
        [b"j", b"l", b"i", b"\x19", b"\x1b"],
    )
    require(exit_code == 0, f"insert-ctrl-y-mid vi exited with status {exit_code}")
    require("-- INSERT --" in decoded, "missing insert-ctrl-y-mid insert status")
    require(saved == "abc\ndbef\nghi\n",
            f"unexpected insert-ctrl-y-mid buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "abc\ndef\nghi\n",
        [b"j", b"l", b"i", b"\x05", b"\x1b"],
    )
    require(exit_code == 0, f"insert-ctrl-e-mid vi exited with status {exit_code}")
    require("-- INSERT --" in decoded, "missing insert-ctrl-e-mid insert status")
    require(saved == "abc\ndhef\nghi\n",
            f"unexpected insert-ctrl-e-mid buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "abc\nd\nghi\n",
        [b"0", b"i", b"\x19", b"\x1b"],
    )
    require(exit_code == 0, f"insert-ctrl-y-top vi exited with status {exit_code}")
    require("-- INSERT --" in decoded, "missing insert-ctrl-y-top insert status")
    require(saved == "abc\nd\nghi\n",
            f"unexpected insert-ctrl-y-top buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "abc\nd\nghi\n",
        [b"G", b"0", b"i", b"\x05", b"\x1b"],
    )
    require(exit_code == 0, f"insert-ctrl-e-bottom vi exited with status {exit_code}")
    require("-- INSERT --" in decoded, "missing insert-ctrl-e-bottom insert status")
    require(saved == "abc\nd\nghi\n",
            f"unexpected insert-ctrl-e-bottom buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "abc\n",
        [b"l", b"a", b"X", b"Y", b"\x15", b"Z", b"\x1b"],
    )
    require(exit_code == 0, f"append-ctrl-u vi exited with status {exit_code}")
    require("-- INSERT --" in decoded, "missing append-ctrl-u insert status")
    require(saved == "abZc\n",
            f"unexpected append-ctrl-u buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "abc\n",
        [b"l", b"a", b"\x14", b"X", b"\x15", b"Y", b"\x1b"],
    )
    require(exit_code == 0, f"append-ctrl-t-ctrl-u vi exited with status {exit_code}")
    require("-- INSERT --" in decoded, "missing append-ctrl-t-ctrl-u insert status")
    require(saved == "\tabYc\n",
            f"unexpected append-ctrl-t-ctrl-u buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\n",
        [b"i", b"X", b"\x1b", b"Z", b"Z"],
        final_keys=None,
    )
    require(exit_code == 0, f"ZZ vi exited with status {exit_code}")
    require(saved == "Xone\n",
            f"unexpected ZZ buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\n",
        [b"i", b"X", b"\x1b", b"Z", b"Q"],
        final_keys=None,
    )
    require(exit_code == 0, f"ZQ vi exited with status {exit_code}")
    require(saved == "one\n",
            f"unexpected ZQ buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "  abc\n",
        [b"I", b"X", b"\x1b"],
    )
    require(exit_code == 0, f"I-first-nonblank vi exited with status {exit_code}")
    require("-- INSERT --" in decoded, "missing I-first-nonblank insert status")
    require(saved == "  Xabc\n",
            f"unexpected I-first-nonblank buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "  abc\n",
        [b"g", b"I", b"X", b"\x1b"],
    )
    require(exit_code == 0, f"gI-basic vi exited with status {exit_code}")
    require("-- INSERT --" in decoded, "missing gI-basic insert status")
    require(saved == "X  abc\n",
            f"unexpected gI-basic buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "",
        [b"i", b"X", b"Y", b"\x1b"],
    )
    require(exit_code == 0, f"empty-insert vi exited with status {exit_code}")
    require("-- INSERT --" in decoded, "missing empty-insert insert status")
    require(saved == "XY\n",
            f"unexpected empty-insert buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "abc\n",
        [b"i", b"X", b"Y", b"\x1b", b"0", b"g", b"i", b"Z", b"\x1b"],
    )
    require(exit_code == 0, f"gi-basic vi exited with status {exit_code}")
    require(decoded.count("-- INSERT --") >= 2, "missing gi-basic insert re-entry status")
    require(saved == "XYZabc\n",
            f"unexpected gi-basic buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "line\n",
        [b"i", b"a", b"b", b"c", b"\x1b", b"A", b"\x01", b"\x1b"],
    )
    require(exit_code == 0, f"insert-ctrl-a vi exited with status {exit_code}")
    require(decoded.count("-- INSERT --") >= 2, "missing insert-ctrl-a insert status")
    require(saved == "abclineabc\n",
            f"unexpected insert-ctrl-a buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "alpha beta\n",
        [b"y", b"w", b"A", b"\x12", b'"', b"\x1b"],
    )
    require(exit_code == 0, f"insert-ctrl-r vi exited with status {exit_code}")
    require("-- INSERT --" in decoded, "missing insert-ctrl-r insert status")
    require(saved == "alpha betaalpha \n",
            f"unexpected insert-ctrl-r buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\nalpha beta\n",
        [b"j", b"y", b"w", b"k", b"0", b"R", b"\x12", b'"', b"\x1b"],
    )
    require(exit_code == 0, f"replace-ctrl-r vi exited with status {exit_code}")
    require("-- REPLACE --" in decoded, "missing replace-ctrl-r replace status")
    require(saved == "alpha \nalpha beta\n",
            f"unexpected replace-ctrl-r buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\n",
        [b"i", b"\x16", b"\x01", b"\x1b"],
    )
    require(exit_code == 0, f"insert-ctrl-v vi exited with status {exit_code}")
    require("-- INSERT --" in decoded, "missing insert-ctrl-v insert status")
    require(saved == "\x01one\n",
            f"unexpected insert-ctrl-v buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\n",
        [b"R", b"\x16", b"\x01", b"\x1b"],
    )
    require(exit_code == 0, f"replace-ctrl-v vi exited with status {exit_code}")
    require("-- REPLACE --" in decoded, "missing replace-ctrl-v replace status")
    require(saved == "\x01ne\n",
            f"unexpected replace-ctrl-v buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "",
        [b"i", b"\r", b"\x1b"],
    )
    require(exit_code == 0, f"empty-enter vi exited with status {exit_code}")
    require("line 1/1" in decoded, "missing empty-enter initial line status")
    require("-- INSERT --" in decoded, "missing empty-enter insert status")
    require(saved == "\n",
            f"unexpected empty-enter buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "abc\n",
        [b"i", b"X", b"\x1b[A", b"\x1b[B", b"\x1b[C", b"\x1b[D", b"Y", b"\x1b"],
    )
    require(exit_code == 0, f"insert-arrow vi exited with status {exit_code}")
    require("-- INSERT --" in decoded, "missing insert-arrow insert status")
    require(saved == "XYabc\n",
            f"unexpected insert-arrow buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "abc\n",
        [b"i", b"X", b"\x1bOA", b"\x1bOB", b"\x1bOC", b"\x1bOD", b"Y", b"\x1b"],
    )
    require(exit_code == 0, f"insert-app-arrow vi exited with status {exit_code}")
    require("-- INSERT --" in decoded, "missing insert-app-arrow insert status")
    require(saved == "XYabc\n",
            f"unexpected insert-app-arrow buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "abcde\n",
        [b"i", b"X", b"\x1b[H", b"Y", b"\x1b[F", b"Z", b"\x1b"],
    )
    require(exit_code == 0, f"insert-home-end vi exited with status {exit_code}")
    require("-- INSERT --" in decoded, "missing insert-home-end insert status")
    require(saved == "YXabcdeZ\n",
            f"unexpected insert-home-end buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "abcde\n",
        [b"i", b"X", b"\x1bOH", b"Y", b"\x1bOF", b"Z", b"\x1b"],
    )
    require(exit_code == 0, f"insert-app-home-end vi exited with status {exit_code}")
    require("-- INSERT --" in decoded, "missing insert-app-home-end insert status")
    require(saved == "YXabcdeZ\n",
            f"unexpected insert-app-home-end buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "abcde\n",
        [b"i", b"\x1b[C", b"\x1b[C", b"\x1b[3~", b"\x1b"],
    )
    require(exit_code == 0, f"insert-delete vi exited with status {exit_code}")
    require("-- INSERT --" in decoded, "missing insert-delete insert status")
    require(saved == "abde\n",
            f"unexpected insert-delete buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "abc\n",
        [b"i", b"\x1b[999~", b"X", b"\x1b"],
    )
    require(exit_code == 0, f"insert-unknown-escape vi exited with status {exit_code}")
    require("-- INSERT --" in decoded, "missing insert-unknown-escape insert status")
    require(saved == "Xabc\n",
            f"unexpected insert-unknown-escape buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\ntwo\n",
        [b"j", b"i", b"\x7f", b"\x1b"],
    )
    require(exit_code == 0, f"insert-backspace-join vi exited with status {exit_code}")
    require("-- INSERT --" in decoded, "missing insert-backspace-join insert status")
    require(saved == "onetwo\n",
            f"unexpected insert-backspace-join buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one two three\n",
        [b"i", b"\x1b[1;5C", b"X", b"\x1b"],
    )
    require(exit_code == 0, f"insert-ctrl-right vi exited with status {exit_code}")
    require("-- INSERT --" in decoded, "missing insert-ctrl-right insert status")
    require(saved == "one Xtwo three\n",
            f"unexpected insert-ctrl-right buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one two three\n",
        [b"A", b"\x1b[1;5D", b"\x1b[127;5u", b"X", b"\x1b"],
    )
    require(exit_code == 0, f"insert-ctrl-backspace vi exited with status {exit_code}")
    require("-- INSERT --" in decoded, "missing insert-ctrl-backspace insert status")
    require(saved == "one Xthree\n",
            f"unexpected insert-ctrl-backspace buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one two three\n",
        [b"A", b"\x1b[1;5D", b"X", b"\x1b"],
    )
    require(exit_code == 0, f"insert-ctrl-left vi exited with status {exit_code}")
    require("-- INSERT --" in decoded, "missing insert-ctrl-left insert status")
    require(saved == "one two Xthree\n",
            f"unexpected insert-ctrl-left buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one two three\n",
        [b"A", b"\x17", b"X", b"\x1b"],
    )
    require(exit_code == 0, f"insert-ctrl-w vi exited with status {exit_code}")
    require("-- INSERT --" in decoded, "missing insert-ctrl-w insert status")
    require(saved == "one two X\n",
            f"unexpected insert-ctrl-w buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one two three\n",
        [b"i", b"\x1b[1;5C", b"\x1b[3;5~", b"X", b"\x1b"],
    )
    require(exit_code == 0, f"insert-ctrl-delete vi exited with status {exit_code}")
    require("-- INSERT --" in decoded, "missing insert-ctrl-delete insert status")
    require(saved == "one Xthree\n",
            f"unexpected insert-ctrl-delete buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "abc def\n",
        [b"i", b"X", b"\x0f", b"x", b"Y", b"\x1b"],
    )
    require(exit_code == 0, f"insert-ctrl-o-delete vi exited with status {exit_code}")
    require("-- INSERT --" in decoded, "missing insert-ctrl-o-delete insert status")
    require(saved == "XYbc def\n",
            f"unexpected insert-ctrl-o-delete buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "abc def\n",
        [b"i", b"X", b"\x0f", b"w", b"Y", b"\x1b"],
    )
    require(exit_code == 0, f"insert-ctrl-o-motion vi exited with status {exit_code}")
    require("-- INSERT --" in decoded, "missing insert-ctrl-o-motion insert status")
    require(saved == "Xabc Ydef\n",
            f"unexpected insert-ctrl-o-motion buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "abc def\n",
        [b"i", b"X", b"\x0f", b":", b"set nu\r", b"Y", b"\x1b"],
    )
    require(exit_code == 0, f"insert-ctrl-o-ex vi exited with status {exit_code}")
    require("-- INSERT --" in decoded, "missing insert-ctrl-o-ex insert status")
    require(":set nu" in decoded, "missing insert-ctrl-o-ex command prompt output")
    require(saved == "XYabc def\n",
            f"unexpected insert-ctrl-o-ex buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "abc def\n",
        [b"i", b"X", b"\x0f", b"d", b"w", b"Y", b"\x1b"],
    )
    require(exit_code == 0, f"insert-ctrl-o-dw vi exited with status {exit_code}")
    require("-- INSERT --" in decoded, "missing insert-ctrl-o-dw insert status")
    require(saved == "XYdef\n",
            f"unexpected insert-ctrl-o-dw buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\ntwo\nthree\n",
        [b"G", b"A", b"X", b"\x0f", b"g", b"g", b"Y", b"\x1b"],
    )
    require(exit_code == 0, f"insert-ctrl-o-gg vi exited with status {exit_code}")
    require("-- INSERT --" in decoded, "missing insert-ctrl-o-gg insert status")
    require(saved == "Yone\ntwo\nthreeX\n",
            f"unexpected insert-ctrl-o-gg buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "0123456789abcdefghijKLMNOPQRST\n",
        [b"2", b"5", b"l", b"r", b"Z"],
        rows=8,
        cols=20,
    )
    require(exit_code == 0, f"long-line vi exited with status {exit_code}")
    require("KLMNO" in decoded, "missing horizontally scrolled long-line content")
    require(saved == "0123456789abcdefghijKLMNOZQRST\n",
            f"unexpected long-line buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "0123456789abcdefghijKLMNOPQRST\n",
        [b":set number\r", b"2", b"5", b"l", b"r", b"Z"],
        rows=8,
        cols=20,
    )
    require(exit_code == 0, f"number-long-line vi exited with status {exit_code}")
    require("      1 efghijKLMNOZ" in decoded,
            "missing numbered horizontally scrolled long-line content")
    require(saved == "0123456789abcdefghijKLMNOZQRST\n",
            f"unexpected number-long-line buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "a\tbcdefghijklmnopqrstuvwxyz\n",
        [b":set list\r", b"2", b"5", b"|", b"r", b"Z"],
        rows=8,
        cols=16,
    )
    require(exit_code == 0, f"list-long-line vi exited with status {exit_code}")
    require("defghijklmnopqrstuvZ" in decoded,
            "missing list-mode horizontally scrolled long-line content")
    require(saved == "a\tbcdefghijklmnopqrstuvZxyz\n",
            f"unexpected list-long-line buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "0123456789abcdefghijKLMNOPQRST\n",
        [("winsize", 8, 28, 0.5)],
        rows=8,
        cols=16,
        final_keys=b":q\r",
    )
    require(exit_code == 0, f"resize vi exited with status {exit_code}")
    require("0123456789abcdefghijKLMNOPQ" in decoded,
            "missing immediate wide-line repaint after resize")
    require(saved == "0123456789abcdefghijKLMNOPQRST\n",
            f"unexpected resize buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "alpha beta gamma\n",
        [b"A", ("winsize", 8, 28, 0.5), b"Z", b"\x1b"],
        rows=8,
        cols=16,
    )
    require(exit_code == 0, f"insert-resize vi exited with status {exit_code}")
    require("-- INSERT --" in decoded, "missing insert-mode status during resize test")
    require(saved == "alpha beta gammaZ\n",
            f"unexpected insert-resize buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "alpha beta gamma\n",
        [b"0", b"R", ("winsize", 8, 28, 0.5), b"Z", b"\x1b"],
        rows=8,
        cols=16,
    )
    require(exit_code == 0, f"replace-resize vi exited with status {exit_code}")
    require("-- REPLACE --" in decoded, "missing replace-mode status during resize test")
    require(saved == "Zlpha beta gamma\n",
            f"unexpected replace-resize buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "abc\n",
        [b"0", b"R", b"X", b"Y", b"\x08", b"\x1b"],
    )
    require(exit_code == 0, f"replace-backspace vi exited with status {exit_code}")
    require(saved == "Xbc\n",
            f"unexpected replace-backspace buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "a\tbc\n",
        [b"0", b"R", b"Z", b"\x08", b"\x1b"],
    )
    require(exit_code == 0, f"replace-tab-backspace vi exited with status {exit_code}")
    require(saved == "a\tbc\n",
            f"unexpected replace-tab-backspace buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "abc\n",
        [b"0", b"R", b"\r", b"\x08", b"\x1b"],
    )
    require(exit_code == 0, f"replace-newline-backspace vi exited with status {exit_code}")
    require(saved == "abc\n",
            f"unexpected replace-newline-backspace buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "abc\n",
        [b"0", b"R", b"X", b"Y", b"Z", b"\x08", b"\x1b"],
    )
    require(exit_code == 0, f"replace-overrun-backspace vi exited with status {exit_code}")
    require(saved == "XYc\n",
            f"unexpected replace-overrun-backspace buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "a\n",
        [b"0", b"R", b"X", b"Y", b"Z", b"\x08", b"\x1b"],
    )
    require(exit_code == 0, f"replace-short-backspace vi exited with status {exit_code}")
    require(saved == "XY\n",
            f"unexpected replace-short-backspace buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "abc\n",
        [b"0", b"R", b"X", b"Y", b"\x1b[D", b"Z", b"\x08", b"\x1b"],
    )
    require(exit_code == 0, f"replace-mixed-backspace vi exited with status {exit_code}")
    require(saved == "XYc\n",
            f"unexpected replace-mixed-backspace buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "abc def\n",
        [b"0", b"R", b"X", b"Y", b"\x15", b"\x1b"],
    )
    require(exit_code == 0, f"replace-ctrl-u vi exited with status {exit_code}")
    require("-- REPLACE --" in decoded, "missing replace-ctrl-u replace status")
    require(saved == "abc def\n",
            f"unexpected replace-ctrl-u buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "abc\ndef\nghi\n",
        [b"j", b"0", b"R", b"\x19", b"\x1b"],
    )
    require(exit_code == 0, f"replace-ctrl-y vi exited with status {exit_code}")
    require("-- REPLACE --" in decoded, "missing replace-ctrl-y replace status")
    require(saved == "abc\naef\nghi\n",
            f"unexpected replace-ctrl-y buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "abc\ndef\nghi\n",
        [b"j", b"0", b"R", b"\x05", b"\x1b"],
    )
    require(exit_code == 0, f"replace-ctrl-e vi exited with status {exit_code}")
    require("-- REPLACE --" in decoded, "missing replace-ctrl-e replace status")
    require(saved == "abc\ngef\nghi\n",
            f"unexpected replace-ctrl-e buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "abc def\n",
        [b"0", b"R", b"X", b"Y", b"\x17", b"\x1b"],
    )
    require(exit_code == 0, f"replace-ctrl-w vi exited with status {exit_code}")
    require("-- REPLACE --" in decoded, "missing replace-ctrl-w replace status")
    require(saved == "abc def\n",
            f"unexpected replace-ctrl-w buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "abc def\n",
        [b"0", b"R", b"X", b"Y", b"\x1b[127;5u", b"\x1b"],
    )
    require(exit_code == 0, f"replace-ctrl-backspace vi exited with status {exit_code}")
    require("-- REPLACE --" in decoded, "missing replace-ctrl-backspace replace status")
    require(saved == "abc def\n",
            f"unexpected replace-ctrl-backspace buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "abc def\n",
        [b"0", b"R", b"X", b"Y", b"\x1b[3;5~", b"\x1b"],
    )
    require(exit_code == 0, f"replace-ctrl-delete vi exited with status {exit_code}")
    require("-- REPLACE --" in decoded, "missing replace-ctrl-delete replace status")
    require(saved == "XYdef\n",
            f"unexpected replace-ctrl-delete buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one two three\n",
        [b"0", b"R", b"X", b"\x1b[1;5C", b"Y", b"\x1b[1;5D", b"Z", b"\x1b"],
    )
    require(exit_code == 0, f"replace-ctrl-left-right vi exited with status {exit_code}")
    require("-- REPLACE --" in decoded, "missing replace-ctrl-left-right replace status")
    require(saved == "Xne Zwo three\n",
            f"unexpected replace-ctrl-left-right buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "abc def\n",
        [b"0", b"R", b"X", b"Y", b"\x1b[H", b"Z", b"\x1b[F", b"\x1b[3~", b"\x1b"],
    )
    require(exit_code == 0, f"replace-home-end-delete vi exited with status {exit_code}")
    require("-- REPLACE --" in decoded, "missing replace-home-end-delete replace status")
    require(saved == "ZYc def\n",
            f"unexpected replace-home-end-delete buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "abc\n",
        [b"0", b"R", b"\x14", b"X", b"\x1b"],
    )
    require(exit_code == 0, f"replace-ctrl-t vi exited with status {exit_code}")
    require("-- REPLACE --" in decoded, "missing replace-ctrl-t replace status")
    require(saved == "\tXbc\n",
            f"unexpected replace-ctrl-t buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "\tabc\n",
        [b"0", b"R", b"\x04", b"X", b"\x1b"],
    )
    require(exit_code == 0, f"replace-ctrl-d vi exited with status {exit_code}")
    require("-- REPLACE --" in decoded, "missing replace-ctrl-d replace status")
    require(saved == "Xbc\n",
            f"unexpected replace-ctrl-d buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "abc\n",
        [b"i", b"X", b"Y", b"\x1b", b"u", b"\x12"],
    )
    require(exit_code == 0, f"insert-undo-redo vi exited with status {exit_code}")
    require(saved == "XYabc\n",
            f"unexpected insert-undo-redo buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "abc\n",
        [b"0", b"R", b"X", b"Y", b"\x1b", b"u", b"\x12"],
    )
    require(exit_code == 0, f"replace-undo-redo vi exited with status {exit_code}")
    require(saved == "XYc\n",
            f"unexpected replace-undo-redo buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\ntwo\n",
        [b"o", b"X", b"\x1b", b"u", b"\x12"],
    )
    require(exit_code == 0, f"open-below-undo-redo vi exited with status {exit_code}")
    require(saved == "one\nX\ntwo\n",
            f"unexpected open-below-undo-redo buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\ntwo\n",
        [b"O", b"X", b"\x1b", b"u", b"\x12"],
    )
    require(exit_code == 0, f"open-above-undo-redo vi exited with status {exit_code}")
    require(saved == "X\none\ntwo\n",
            f"unexpected open-above-undo-redo buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\ntwo\n",
        [b"y", b"y", b"p", b"u", b"\x12"],
    )
    require(exit_code == 0, f"put-undo-redo vi exited with status {exit_code}")
    require(saved == "one\none\ntwo\n",
            f"unexpected put-undo-redo buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\ntwo\nthree\n",
        [b"J", b"u", b"\x12"],
    )
    require(exit_code == 0, f"join-undo-redo vi exited with status {exit_code}")
    require(saved == "one two\nthree\n",
            f"unexpected join-undo-redo buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "alpha beta\n",
        [b"c", b"w", b"X", b"\x1b", b"u", b"\x12"],
    )
    require(exit_code == 0, f"change-undo-redo vi exited with status {exit_code}")
    require(saved == "X beta\n",
            f"unexpected change-undo-redo buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\ntwo\nthree\n",
        [b":", b"1", b"d", b"\r", b"u", b"\x12"],
    )
    require(exit_code == 0, f"ex-delete-undo-redo vi exited with status {exit_code}")
    require(saved == "two\nthree\n",
            f"unexpected ex-delete-undo-redo buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "alpha beta gamma\n",
        [b"d", ("winsize", 8, 28, 0.5), b"w"],
        rows=8,
        cols=16,
    )
    require(exit_code == 0, f"operator-resize vi exited with status {exit_code}")
    require(saved == "beta gamma\n",
            f"unexpected operator-resize buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\ntwo\nthree\n",
        [b":", ("winsize", 8, 28, 0.5), b"1d\r"],
        rows=8,
        cols=16,
    )
    require(exit_code == 0, f"colon-prompt-resize vi exited with status {exit_code}")
    require(saved == "two\nthree\n",
            f"unexpected colon-prompt-resize buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\ntwo\nthree\n",
        [b"/", ("winsize", 8, 28, 0.5), b"two\r", b"r", b"Z"],
        rows=8,
        cols=16,
    )
    require(exit_code == 0, f"search-prompt-resize vi exited with status {exit_code}")
    require(saved == "one\nZwo\nthree\n",
            f"unexpected search-prompt-resize buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\ntwo\nthree\n",
        [b"G", b"?", ("winsize", 8, 28, 0.5), b"two\r", b"r", b"Z"],
        rows=8,
        cols=16,
    )
    require(exit_code == 0, f"backward-search-prompt-resize vi exited with status {exit_code}")
    require(saved == "one\nZwo\nthree\n",
            f"unexpected backward-search-prompt-resize buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "0123456789abcdefghijKLMNOPQRST\n",
        [("winsize", 8, 28, 0.3), ("winsize", 8, 18, 0.3), ("winsize", 8, 30, 0.3),
         b"3", b"0", b"|", b"r", b"Z"],
        rows=8,
        cols=16,
    )
    require(exit_code == 0, f"repeated-resize vi exited with status {exit_code}")
    require(saved == "0123456789abcdefghijKLMNOPQRSZ\n",
            f"unexpected repeated-resize buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\ntwo\nthree\n",
        [b"j", b"r", b"Z"],
        rows=4,
        cols=8,
    )
    require(exit_code == 0, f"narrow-terminal vi exited with status {exit_code}")
    require(saved == "one\nZwo\nthree\n",
            f"unexpected narrow-terminal buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "0123456789abcdefghijKLMNOPQRST\n",
        [b":set number\r"],
        rows=4,
        cols=12,
        final_keys=b":q\r",
    )
    require(exit_code == 0, f"narrow-status vi exited with status {exit_code}")
    require("buffer.txt\"  line 1/1" not in decoded,
            "status line wrapped full filename/details in narrow terminal")
    require(saved == "0123456789abcdefghijKLMNOPQRST\n",
            f"unexpected narrow-status buffer: {saved!r}")

    long_file = "".join(f"line {i}\n" for i in range(1, 121))
    exit_code, decoded, saved = run_vi_session(
        vi_path,
        long_file,
        [b"G", b"r", b"Z"],
        rows=8,
        cols=24,
    )
    require(exit_code == 0, f"long-file vi exited with status {exit_code}")
    require("line 120/120" in decoded, "missing long-file final-line status")
    require(saved.endswith("Zine 120\n"),
            f"unexpected long-file buffer tail: {saved[-16:]!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "alpha\nbeta\ngamma\n",
        [b"j", b"l", b"h", b"k"],
        final_keys=b":q\r",
    )
    require(exit_code == 0, f"cursor-only redraw vi exited with status {exit_code}")
    require(decoded.count("\x1b[?25l\x1b[H") == 1,
            "cursor-only motions triggered a full body redraw")
    require("line 2/3" in decoded, "missing status update during cursor-only motion")
    require(saved == "alpha\nbeta\ngamma\n",
            f"unexpected cursor-only redraw buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "alpha\nbeta\ngamma\n",
        [b"j", b"r", b"Z"],
        final_keys=b":wq\r",
    )
    require(exit_code == 0, f"text-change redraw vi exited with status {exit_code}")
    require(decoded.count("\x1b[?25l\x1b[H") >= 2,
            "text-changing edit did not trigger a body redraw")
    require(saved == "alpha\nZeta\ngamma\n",
            f"unexpected text-change redraw buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "redraw me\nsecond line\n",
        [b"j", b"\x0c"],
        final_keys=b":q\r",
    )
    require(exit_code == 0, f"ctrl-l redraw vi exited with status {exit_code}")
    require(decoded.count("\x1b[2J") >= 2, "missing explicit Ctrl-L full-screen redraw")
    require("line 2/2" in decoded, "missing redraw status after Ctrl-L")
    require(saved == "redraw me\nsecond line\n",
            f"unexpected ctrl-l redraw buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "a\tb\n",
        [],
        rows=8,
        cols=16,
    )
    require(exit_code == 0, f"tab-render vi exited with status {exit_code}")
    require("a       b" in decoded, "missing expanded tab rendering")
    require("^I" not in decoded, "rendered tabs as caret escapes in normal visual mode")
    require(saved == "a\tb\n",
            f"unexpected tab-render buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "a\tb\n",
        [b":", b"set ts=4\r"],
        rows=8,
        cols=16,
    )
    require(exit_code == 0, f"tab-render-set vi exited with status {exit_code}")
    require("a   b" in decoded, "missing tabstop=4 rendering")
    require(saved == "a\tb\n",
            f"unexpected tab-render-set buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "0123456789abcdefghijKLMNOPQRST\n",
        [b"3", b"0", b"|", b"r", b"Z"],
        rows=8,
        cols=20,
    )
    require(exit_code == 0, f"column-motion vi exited with status {exit_code}")
    require("NOPQRSZ" in decoded, "missing far-right long-line content")
    require(saved == "0123456789abcdefghijKLMNOPQRSZ\n",
            f"unexpected column-motion buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "a\tbcdefghijklmnopqrstuvwxyz\n",
        [b"2", b"5", b"|", b"r", b"Z"],
        rows=8,
        cols=16,
    )
    require(exit_code == 0, f"tab-column-motion vi exited with status {exit_code}")
    require("bcdefghijklmnopqZ" in decoded, "missing tab-aware far-right long-line content")
    require(saved == "a\tbcdefghijklmnopqZstuvwxyz\n",
            f"unexpected tab-column-motion buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "0123456789abcdefghijKLMNOPQRST\n",
        [b"d", b"3", b"0", b"|"],
        rows=8,
        cols=20,
    )
    require(exit_code == 0, f"column-delete vi exited with status {exit_code}")
    require(saved == "T\n",
            f"unexpected column-delete buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "a\tbcdefghijklmnopqrstuvwxyz\n",
        [b"c", b"2", b"5", b"|", b"X", b"\x1b"],
        rows=8,
        cols=16,
    )
    require(exit_code == 0, f"tab-column-change vi exited with status {exit_code}")
    require("-- INSERT --" in decoded, "missing tab-column change insert status")
    require(saved == "Xrstuvwxyz\n",
            f"unexpected tab-column-change buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "abc   \ndef   \n",
        [b"g", b"_", b"r", b"Z", b"2", b"g", b"_", b"r", b"Y"],
    )
    require(exit_code == 0, f"g_-motion vi exited with status {exit_code}")
    require("line 2/2" in decoded, "missing counted g_ status")
    require(saved == "abZ   \ndeY   \n",
            f"unexpected g_-motion buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "aa\n  bb\ncc\n",
        [b"2", b"_", b"r", b"X"],
    )
    require(exit_code == 0, f"underscore-motion vi exited with status {exit_code}")
    require("line 2/3" in decoded, "missing underscore-motion status")
    require(saved == "aa\n  Xb\ncc\n",
            f"unexpected underscore-motion buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\ntwo\nthree\n",
        [b"d", b"_"],
    )
    require(exit_code == 0, f"d_ vi exited with status {exit_code}")
    require(saved == "two\nthree\n",
            f"unexpected d_ buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\ntwo\nthree\n",
        [b"c", b"_", b"X", b"\x1b"],
    )
    require(exit_code == 0, f"c_ vi exited with status {exit_code}")
    require("-- INSERT --" in decoded, "missing c_ insert status")
    require(saved == "X\ntwo\nthree\n",
            f"unexpected c_ buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\ntwo\n",
        [b"y", b"_", b"P"],
    )
    require(exit_code == 0, f"y_ vi exited with status {exit_code}")
    require(saved == "one\none\ntwo\n",
            f"unexpected y_ buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\ntwo\nthree\nfour\n",
        [b"d", b"j"],
    )
    require(exit_code == 0, f"dj vi exited with status {exit_code}")
    require(saved == "three\nfour\n",
            f"unexpected dj buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\ntwo\nthree\nfour\n",
        [b"j", b"j", b"y", b"k", b"P"],
    )
    require(exit_code == 0, f"yk vi exited with status {exit_code}")
    require(saved == "one\ntwo\ntwo\nthree\nthree\nfour\n",
            f"unexpected yk buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\ntwo\nthree\n",
        [b"y", b"+", b"P"],
    )
    require(exit_code == 0, f"y+ vi exited with status {exit_code}")
    require(saved == "one\ntwo\none\ntwo\nthree\n",
            f"unexpected y+ buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "abcde\n",
        [b"y", b"2", b"l", b"P", b"r", b"Z"],
    )
    require(exit_code == 0, f"y2lP vi exited with status {exit_code}")
    require(saved == "aZabcde\n",
            f"unexpected y2lP buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "abcde\n",
        [b"y", b"2", b"l", b"p", b"r", b"Z"],
    )
    require(exit_code == 0, f"y2lp vi exited with status {exit_code}")
    require(saved == "aaZbcde\n",
            f"unexpected y2lp buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "abcde\n",
        [b"d", b"2", b"l", b"P", b"r", b"Z"],
    )
    require(exit_code == 0, f"d2lP vi exited with status {exit_code}")
    require(saved == "aZcde\n",
            f"unexpected d2lP buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "abcde\n",
        [b"d", b"2", b"l", b"p", b"r", b"Z"],
    )
    require(exit_code == 0, f"d2lp vi exited with status {exit_code}")
    require(saved == "caZde\n",
            f"unexpected d2lp buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\ntwo\n",
        [b"y", b"y", b"P", b"r", b"Z"],
    )
    require(exit_code == 0, f"yyP vi exited with status {exit_code}")
    require(saved == "Zne\none\ntwo\n",
            f"unexpected yyP buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\ntwo\n",
        [b"y", b"y", b"p", b"r", b"Z"],
    )
    require(exit_code == 0, f"yyp vi exited with status {exit_code}")
    require(saved == "one\nZne\ntwo\n",
            f"unexpected yyp buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\ntwo\n",
        [b"d", b"d", b"P", b"r", b"Z"],
    )
    require(exit_code == 0, f"ddP vi exited with status {exit_code}")
    require(saved == "Zne\ntwo\n",
            f"unexpected ddP buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\ntwo\n",
        [b"d", b"d", b"p", b"r", b"Z"],
    )
    require(exit_code == 0, f"ddp vi exited with status {exit_code}")
    require(saved == "two\nZne\n",
            f"unexpected ddp buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "abc def\n",
        [b"c", b"w", b"X", b"\x1b", b"P", b"r", b"Z"],
    )
    require(exit_code == 0, f"cwP vi exited with status {exit_code}")
    require(saved == "abZX def\n",
            f"unexpected cwP buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "abc def\n",
        [b"c", b"w", b"X", b"\x1b", b"p", b"r", b"Z"],
    )
    require(exit_code == 0, f"cwp vi exited with status {exit_code}")
    require(saved == "XabZ def\n",
            f"unexpected cwp buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "abc def ghi\n",
        [b"c", b"2", b"w", b"X", b"\x1b", b"P", b"r", b"Z"],
    )
    require(exit_code == 0, f"c2wP vi exited with status {exit_code}")
    require(saved == "abc deZX ghi\n",
            f"unexpected c2wP buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "abc def ghi\n",
        [b"c", b"2", b"w", b"X", b"\x1b", b"p", b"r", b"Z"],
    )
    require(exit_code == 0, f"c2wp vi exited with status {exit_code}")
    require(saved == "Xabc deZ ghi\n",
            f"unexpected c2wp buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "abcde\n",
        [b"s", b"X", b"\x1b", b"P", b"r", b"Z"],
    )
    require(exit_code == 0, f"sP vi exited with status {exit_code}")
    require(saved == "ZXbcde\n",
            f"unexpected sP buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "abcde\n",
        [b"s", b"X", b"\x1b", b"p", b"r", b"Z"],
    )
    require(exit_code == 0, f"sp vi exited with status {exit_code}")
    require(saved == "XZbcde\n",
            f"unexpected sp buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "abc def\n",
        [b"C", b"X", b"Y", b"Z", b"\x1b", b"P", b"r", b"Z"],
    )
    require(exit_code == 0, f"CP vi exited with status {exit_code}")
    require(saved == "XYabc deZZ\n",
            f"unexpected CP buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "abc def\n",
        [b"C", b"X", b"Y", b"Z", b"\x1b", b"p", b"r", b"Z"],
    )
    require(exit_code == 0, f"Cp vi exited with status {exit_code}")
    require(saved == "XYZabc deZ\n",
            f"unexpected Cp buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\ntwo\nthree\nfour\n",
        [b"c", b"+", b"X", b"\x1b"],
    )
    require(exit_code == 0, f"c+ vi exited with status {exit_code}")
    require("-- INSERT --" in decoded, "missing c+ insert status")
    require(saved == "X\nthree\nfour\n",
            f"unexpected c+ buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\ntwo\nthree\nfour\n",
        [b"j", b"j", b"d", b"-"],
    )
    require(exit_code == 0, f"d- vi exited with status {exit_code}")
    require(saved == "one\nfour\n",
            f"unexpected d- buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\ntwo\nthree\nfour\n",
        [b"d", b"G"],
    )
    require(exit_code == 0, f"dG vi exited with status {exit_code}")
    require(saved == "",
            f"unexpected dG buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\ntwo\nthree\nfour\n",
        [b"3", b"y", b"G", b"P"],
    )
    require(exit_code == 0, f"3yG vi exited with status {exit_code}")
    require(saved == "one\ntwo\nthree\none\ntwo\nthree\nfour\n",
            f"unexpected 3yG buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\ntwo\nthree\nfour\n",
        [b"j", b"c", b"G", b"X", b"\x1b"],
    )
    require(exit_code == 0, f"cG vi exited with status {exit_code}")
    require("-- INSERT --" in decoded, "missing cG insert status")
    require(saved == "one\nX\n",
            f"unexpected cG buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\ntwo\nthree\nfour\n",
        [b"j", b"j", b"d", b"g", b"g"],
    )
    require(exit_code == 0, f"dgg vi exited with status {exit_code}")
    require(saved == "four\n",
            f"unexpected dgg buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\ntwo\nthree\nfour\n",
        [b"j", b"j", b"y", b"g", b"g", b"P"],
    )
    require(exit_code == 0, f"ygg vi exited with status {exit_code}")
    require(saved == "one\ntwo\none\ntwo\nthree\nthree\nfour\n",
            f"unexpected ygg buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\ntwo\nthree\nfour\n",
        [b"j", b"j", b"c", b"g", b"g", b"X", b"\x1b"],
    )
    require(exit_code == 0, f"cgg vi exited with status {exit_code}")
    require("-- INSERT --" in decoded, "missing cgg insert status")
    require(saved == "X\nfour\n",
            f"unexpected cgg buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\ntwo\nthree\nfour\nfive\nsix\nseven\neight\n",
        [b"j", b"j", b"j", b"d", b"H"],
        rows=6,
    )
    require(exit_code == 0, f"dH vi exited with status {exit_code}")
    require(saved == "five\nsix\nseven\neight\n",
            f"unexpected dH buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\ntwo\nthree\nfour\nfive\nsix\nseven\neight\n",
        [b"j", b"j", b"d", b"M"],
        rows=7,
    )
    require(exit_code == 0, f"dM vi exited with status {exit_code}")
    require(saved == "one\ntwo\nfour\nfive\nsix\nseven\neight\n",
            f"unexpected dM buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\ntwo\nthree\nfour\nfive\nsix\nseven\neight\n",
        [b"j", b"j", b"j", b"c", b"H", b"X", b"\x1b"],
        rows=6,
    )
    require(exit_code == 0, f"cH vi exited with status {exit_code}")
    require("-- INSERT --" in decoded, "missing cH insert status")
    require(saved == "X\nfive\nsix\nseven\neight\n",
            f"unexpected cH buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\ntwo\nthree\nfour\nfive\nsix\nseven\neight\n",
        [b"j", b"c", b"M", b"X", b"\x1b"],
        rows=7,
    )
    require(exit_code == 0, f"cM vi exited with status {exit_code}")
    require("-- INSERT --" in decoded, "missing cM insert status")
    require(saved == "one\nX\nfour\nfive\nsix\nseven\neight\n",
            f"unexpected cM buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\ntwo\nthree\nfour\nfive\nsix\nseven\neight\n",
        [b"c", b"L", b"X", b"\x1b"],
        rows=6,
    )
    require(exit_code == 0, f"cL vi exited with status {exit_code}")
    require("-- INSERT --" in decoded, "missing cL insert status")
    require(saved == "X\nsix\nseven\neight\n",
            f"unexpected cL buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\ntwo\nthree\nfour\nfive\nsix\nseven\neight\n",
        [b"j", b"2", b"y", b"L", b"P"],
        rows=6,
    )
    require(exit_code == 0, f"2yL vi exited with status {exit_code}")
    require(saved == "one\ntwo\nthree\nfour\ntwo\nthree\nfour\nfive\nsix\nseven\neight\n",
            f"unexpected 2yL buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\ntwo\nthree\nfour\nfive\nsix\nseven\neight\n",
        [b"d", b"L"],
        rows=6,
    )
    require(exit_code == 0, f"dL vi exited with status {exit_code}")
    require(saved == "six\nseven\neight\n",
            f"unexpected dL buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\ntwo\nthree\nfour\nfive\nsix\nseven\neight\n",
        [b"3", b"L", b"r", b"Z", b"2", b"H", b"r", b"Y"],
        rows=6,
    )
    require(exit_code == 0, f"counted-HL vi exited with status {exit_code}")
    require(saved == "one\nYwo\nZhree\nfour\nfive\nsix\nseven\neight\n",
            f"unexpected counted-HL buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "abcd\n",
        [b"l", b"d", b"g", b"_"],
    )
    require(exit_code == 0, f"dg_ vi exited with status {exit_code}")
    require(saved == "a\n",
            f"unexpected dg_ buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "abcd\n",
        [b"l", b"c", b"g", b"_", b"X", b"\x1b"],
    )
    require(exit_code == 0, f"cg_ vi exited with status {exit_code}")
    require("-- INSERT --" in decoded, "missing cg_ insert status")
    require(saved == "aX\n",
            f"unexpected cg_ buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "abcd\n",
        [b"l", b"y", b"g", b"_", b"P"],
    )
    require(exit_code == 0, f"yg_ vi exited with status {exit_code}")
    require(saved == "abcdbcd\n",
            f"unexpected yg_ buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "One. Two. Three.\n",
        [b"d", b")"],
    )
    require(exit_code == 0, f"d) vi exited with status {exit_code}")
    require(saved == "Two. Three.\n",
            f"unexpected d) buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "One. Two. Three.\n",
        [b"c", b")", b"X", b"\x1b"],
    )
    require(exit_code == 0, f"c) vi exited with status {exit_code}")
    require("-- INSERT --" in decoded, "missing c) insert status")
    require(saved == "XTwo. Three.\n",
            f"unexpected c) buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "One.\nTwo.\nThree.\n",
        [b"G", b"$", b"c", b"(", b"X", b"\x1b"],
    )
    require(exit_code == 0, f"c( vi exited with status {exit_code}")
    require("-- INSERT --" in decoded, "missing c( insert status")
    require(saved == "One.\nTwo.\nX.\n",
            f"unexpected c( buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "One.\nTwo.\nThree.\n",
        [b"G", b">", b"("],
    )
    require(exit_code == 0, f">( vi exited with status {exit_code}")
    require(saved == "One.\n\tTwo.\nThree.\n",
            f"unexpected >( buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "\tOne.\n\tTwo.\n\tThree.\n",
        [b"G", b"<", b"("],
    )
    require(exit_code == 0, f"<( vi exited with status {exit_code}")
    require(saved == "\tOne.\nTwo.\nThree.\n",
            f"unexpected <( buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "One.\nTwo.\nThree.\n",
        [b">", b")"],
    )
    require(exit_code == 0, f">) vi exited with status {exit_code}")
    require(saved == "\tOne.\nTwo.\nThree.\n",
            f"unexpected >) buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "\tOne.\n\tTwo.\n\tThree.\n",
        [b"<", b")"],
    )
    require(exit_code == 0, f"<) vi exited with status {exit_code}")
    require(saved == "One.\nTwo.\n\tThree.\n",
            f"unexpected <) buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\n\ntwo\nthree\n\nfour\n",
        [b"d", b"}"],
    )
    require(exit_code == 0, f"d}} vi exited with status {exit_code}")
    require(saved == "\ntwo\nthree\n\nfour\n",
            f"unexpected d}} buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\n\ntwo\n\nthree\n",
        [b"G", b"d", b"{"],
    )
    require(exit_code == 0, f"d{{ vi exited with status {exit_code}")
    require(saved == "one\n\ntwo\nthree\n",
            f"unexpected d{{ buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\n\ntwo\n\nthree\n",
        [b"G", b"c", b"{", b"X", b"\x1b"],
    )
    require(exit_code == 0, f"c{{ vi exited with status {exit_code}")
    require("-- INSERT --" in decoded, "missing c{ insert status")
    require(saved == "one\n\ntwo\nX\nthree\n",
            f"unexpected c{{ buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\n\ntwo\n\nthree\n",
        [b"G", b"y", b"{", b"P"],
    )
    require(exit_code == 0, f"y{{ vi exited with status {exit_code}")
    require(saved == "one\n\ntwo\n\n\nthree\n",
            f"unexpected y{{ buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\n\ntwo\n\nthree\n",
        [b"G", b">", b"{"],
    )
    require(exit_code == 0, f">{{ vi exited with status {exit_code}")
    require(saved == "one\n\ntwo\n\nthree\n",
            f"unexpected >{{ buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "\tone\n\t\n\ttwo\n\t\n\tthree\n",
        [b"G", b"<", b"{"],
    )
    require(exit_code == 0, f"<{{ vi exited with status {exit_code}")
    require(saved == "one\n\ntwo\n\nthree\n",
            f"unexpected <{{ buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\n\ntwo\n\nthree\n",
        [b"c", b"}", b"X", b"\x1b"],
    )
    require(exit_code == 0, f"c}} vi exited with status {exit_code}")
    require("-- INSERT --" in decoded, "missing c} insert status")
    require(saved == "X\n\ntwo\n\nthree\n",
            f"unexpected c}} buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\n\ntwo\n\nthree\n",
        [b"y", b"}", b"P"],
    )
    require(exit_code == 0, f"y}} vi exited with status {exit_code}")
    require(saved == "one\none\n\ntwo\n\nthree\n",
            f"unexpected y}} buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\n\ntwo\n\nthree\n",
        [b">", b"}"],
    )
    require(exit_code == 0, f">}} vi exited with status {exit_code}")
    require(saved == "\tone\n\ntwo\n\nthree\n",
            f"unexpected >}} buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "\tone\n\t\n\ttwo\n\t\n\tthree\n",
        [b"<", b"}"],
    )
    require(exit_code == 0, f"<}} vi exited with status {exit_code}")
    require(saved == "one\n\ntwo\n\nthree\n",
            f"unexpected <}} buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "\tone\n\t\n\ttwo\n\t\n\tthree\n",
        [b"}", b"r", b"X"],
    )
    require(exit_code == 0, f"}}-tab-separator vi exited with status {exit_code}")
    require(saved == "\tone\n\t\n\ttwo\n\t\n\tthreX\n",
            f"unexpected }}-tab-separator buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "\tone\n\t\n\ttwo\n\t\n\tthree\n",
        [b">", b"}"],
    )
    require(exit_code == 0, f">}}-tab-separator vi exited with status {exit_code}")
    require(saved == "\t\tone\n\t\t\n\t\ttwo\n\t\t\n\t\tthree\n",
            f"unexpected >}}-tab-separator buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "alpha\nbeta\ngamma\n",
        [b"d", b"/", b"g", b"a", b"m", b"m", b"a", b"\r"],
    )
    require(exit_code == 0, f"d/search vi exited with status {exit_code}")
    require(saved == "\ngamma\n",
            f"unexpected d/search buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "alpha\nbeta\ngamma\n",
        [b"c", b"/", b"g", b"a", b"m", b"m", b"a", b"\r", b"X", b"\x1b"],
    )
    require(exit_code == 0, f"c/search vi exited with status {exit_code}")
    require("-- INSERT --" in decoded, "missing c/search insert status")
    require(saved == "X\ngamma\n",
            f"unexpected c/search buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "alpha\nbeta\ngamma\n",
        [b"y", b"/", b"g", b"a", b"m", b"m", b"a", b"\r", b"P"],
    )
    require(exit_code == 0, f"y/search vi exited with status {exit_code}")
    require(saved == "alpha\nbetaalpha\nbeta\ngamma\n",
            f"unexpected y/search buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "alpha\nbeta\ngamma\n",
        [b"G", b"d", b"?", b"a", b"l", b"p", b"h", b"a", b"\r"],
    )
    require(exit_code == 0, f"d?search vi exited with status {exit_code}")
    require(saved == "gamma\n",
            f"unexpected d?search buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "alpha\nbeta\ngamma\n",
        [b"G", b"c", b"?", b"a", b"l", b"p", b"h", b"a", b"\r", b"X", b"\x1b"],
    )
    require(exit_code == 0, f"c?search vi exited with status {exit_code}")
    require("-- INSERT --" in decoded, "missing c?search insert status")
    require(saved == "X\ngamma\n",
            f"unexpected c?search buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "alpha\nbeta\ngamma\n",
        [b"G", b"y", b"?", b"a", b"l", b"p", b"h", b"a", b"\r", b"P"],
    )
    require(exit_code == 0, f"y?search vi exited with status {exit_code}")
    require(saved == "alpha\nbeta\nalpha\nbeta\ngamma\n",
            f"unexpected y?search buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "aa\nmatch\nbb\nmatch\ncc\n",
        [b">", b"/", b"m", b"a", b"t", b"c", b"h", b"\r"],
    )
    require(exit_code == 0, f">/search vi exited with status {exit_code}")
    require(saved == "\taa\nmatch\nbb\nmatch\ncc\n",
            f"unexpected >/search buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "aa\nmatch\nbb\nmatch\ncc\n",
        [b"/", b"m", b"a", b"t", b"c", b"h", b"\r", b"g", b"g", b">", b"n"],
    )
    require(exit_code == 0, f">n vi exited with status {exit_code}")
    require(saved == "\taa\nmatch\nbb\nmatch\ncc\n",
            f"unexpected >n buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "aa\nmatch\nbb\nmatch\ncc\n",
        [b"g", b"g", b">", b"/", b"m", b"a", b"t", b"c", b"h", b"\r", b"j", b">", b"n"],
    )
    require(exit_code == 0, f"search-shift-repeat vi exited with status {exit_code}")
    require(saved == "\taa\n\tmatch\n\tbb\nmatch\ncc\n",
            f"unexpected search-shift-repeat buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "alpha\nbeta\ngamma\ndelta\nbeta2\n",
        [b"/", b"b", b"e", b"t", b"a", b"\r", b"g", b"g", b"d", b"n"],
    )
    require(exit_code == 0, f"dn vi exited with status {exit_code}")
    require(saved == "beta\ngamma\ndelta\nbeta2\n",
            f"unexpected dn buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "aa\nfoo\nbb\nfoo\ncc\n",
        [b"/", b"f", b"o", b"o", b"\r", b"y", b"n", b"P"],
    )
    require(exit_code == 0, f"yn vi exited with status {exit_code}")
    require(saved == "aa\nfoo\nbb\nfoo\nbb\nfoo\ncc\n",
            f"unexpected yn buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "aa\nfoo\nbb\nfoo\ncc\n",
        [b"G", b"?", b"f", b"o", b"o", b"\r", b"y", b"N", b"P"],
    )
    require(exit_code == 0, f"yN vi exited with status {exit_code}")
    require(saved == "aa\nfoo\nbb\nfoo\nbb\nfoo\ncc\n",
            f"unexpected yN buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "word\nmiddle\nword\n",
        [b"d", b"*"],
    )
    require(exit_code == 0, f"d* vi exited with status {exit_code}")
    require(saved == "word\n",
            f"unexpected d* buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "word\nmiddle\nword\n",
        [b"c", b"*", b"X", b"\x1b"],
    )
    require(exit_code == 0, f"c* vi exited with status {exit_code}")
    require("-- INSERT --" in decoded, "missing c* insert status")
    require(saved == "X\nword\n",
            f"unexpected c* buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "foo\nbar\nfoo\n",
        [b"y", b"*", b"P"],
    )
    require(exit_code == 0, f"y* vi exited with status {exit_code}")
    require(saved == "foo\nbar\nfoo\nbar\nfoo\n",
            f"unexpected y* buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "word\nmiddle\nword\n",
        [b"G", b"d", b"#"],
    )
    require(exit_code == 0, f"d# vi exited with status {exit_code}")
    require(saved == "word\n",
            f"unexpected d# buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "word\nmiddle\nword\n",
        [b"G", b"c", b"#", b"X", b"\x1b"],
    )
    require(exit_code == 0, f"c# vi exited with status {exit_code}")
    require("-- INSERT --" in decoded, "missing c# insert status")
    require(saved == "X\nword\n",
            f"unexpected c# buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "foo\nbar\nfoo\n",
        [b"G", b"y", b"#", b"P"],
    )
    require(exit_code == 0, f"y# vi exited with status {exit_code}")
    require(saved == "foo\nbar\nfoo\nbar\nfoo\n",
            f"unexpected y# buffer: {saved!r}")

    search_operator_cases = [
        ("</search", "\taa\n\tmatch\n\tbb\n\tmatch\n\tcc\n",
            [b"<", b"/", b"match\r"], "aa\nmatch\n\tbb\n\tmatch\n\tcc\n", None),
        (">?search", "\taa\n\tmatch\n\tbb\n\tmatch\n\tcc\n",
            [b"G", b">", b"?", b"match\r"], "\taa\n\tmatch\n\tbb\n\t\tmatch\n\t\tcc\n", None),
        ("<?search", "\taa\n\tmatch\n\tbb\n\tmatch\n\tcc\n",
            [b"G", b"<", b"?", b"match\r"], "\taa\n\tmatch\n\tbb\nmatch\ncc\n", None),
        ("cn", "aa\nmatch\nbb\nmatch\ncc\n",
            [b"/", b"match\r", b"g", b"g", b"c", b"n", b"X", b"\x1b"],
            "X\nmatch\nbb\nmatch\ncc\n", "-- INSERT --"),
        ("dN", "aa\nfoo\nbb\nfoo\ncc\n",
            [b"G", b"?", b"foo\r", b"d", b"N"], "aa\nfoo\ncc\n", None),
        ("cN", "aa\nfoo\nbb\nfoo\ncc\n",
            [b"G", b"?", b"foo\r", b"c", b"N", b"X", b"\x1b"],
            "aa\nX\nfoo\ncc\n", "-- INSERT --"),
        (">N", "\taa\n\tfoo\n\tbb\n\tfoo\n\tcc\n",
            [b"G", b"?", b"foo\r", b"g", b"g", b">", b"N"],
            "\t\taa\n\t\tfoo\n\tbb\n\tfoo\n\tcc\n", None),
        ("<n", "\taa\n\tmatch\n\tbb\n\tmatch\n\tcc\n",
            [b"/", b"match\r", b"g", b"g", b"<", b"n"], "aa\nmatch\n\tbb\n\tmatch\n\tcc\n", None),
        ("<N", "\taa\n\tfoo\n\tbb\n\tfoo\n\tcc\n",
            [b"G", b"?", b"foo\r", b"g", b"g", b"<", b"N"], "aa\nfoo\n\tbb\n\tfoo\n\tcc\n", None),
        (">*", "\tword\n\tmiddle\n\tword\n",
            [b">", b"*"], "\t\tword\n\t\tmiddle\n\t\tword\n", None),
        ("<*", "\tword\n\tmiddle\n\tword\n",
            [b"<", b"*"], "word\nmiddle\nword\n", None),
        (">#", "\tword\n\tmiddle\n\tword\n",
            [b"G", b">", b"#"], "\t\tword\n\t\tmiddle\n\t\tword\n", None),
        ("<#", "\tword\n\tmiddle\n\tword\n",
            [b"G", b"<", b"#"], "word\nmiddle\nword\n", None),
    ]
    for name, initial_text, key_steps, expected, status_text in search_operator_cases:
        exit_code, decoded, saved = run_vi_session(vi_path, initial_text, key_steps)
        require(exit_code == 0, f"{name} vi exited with status {exit_code}")
        if status_text is not None:
            require(status_text in decoded, f"missing {name} status")
        require(saved == expected, f"unexpected {name} buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "a\nmatch1\nx\nmatch2\ny\nmatch3\n",
        [b"/", b"m", b"a", b"t", b"c", b"h", b"\r", b"g", b"g", b"d", b"2", b"n"],
    )
    require(exit_code == 0, f"d2n vi exited with status {exit_code}")
    require(saved == "match2\ny\nmatch3\n",
            f"unexpected d2n buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "word\nx\nword\ny\nword\n",
        [b"g", b"g", b"d", b"2", b"*"],
    )
    require(exit_code == 0, f"d2* vi exited with status {exit_code}")
    require(saved == "word\n",
            f"unexpected d2* buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "word\nx\nword\ny\nword\n",
        [b"G", b"d", b"2", b"#"],
    )
    require(exit_code == 0, f"d2# vi exited with status {exit_code}")
    require(saved == "word\n",
            f"unexpected d2# buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\ntwo\nthree\n",
        [b"2", b"$", b"r", b"X"],
    )
    require(exit_code == 0, f"2$ vi exited with status {exit_code}")
    require(saved == "one\ntwX\nthree\n",
            f"unexpected 2$ buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\ntwo\nthree\n",
        [b"d", b"2", b"$"],
    )
    require(exit_code == 0, f"d2$ vi exited with status {exit_code}")
    require(saved == "three\n",
            f"unexpected d2$ buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\ntwo\nthree\n",
        [b"c", b"2", b"$", b"X", b"\x1b"],
    )
    require(exit_code == 0, f"c2$ vi exited with status {exit_code}")
    require("-- INSERT --" in decoded, "missing c2$ insert status")
    require(saved == "X\nthree\n",
            f"unexpected c2$ buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\ntwo\nthree\n",
        [b"y", b"2", b"$", b"P"],
    )
    require(exit_code == 0, f"y2$ vi exited with status {exit_code}")
    require(saved == "one\ntwoone\ntwo\nthree\n",
            f"unexpected y2$ buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "1\n2\n3\n4\n5\n6\n7\n8\n9\n10\n",
        [b"d", b"5", b"0", b"%"],
    )
    require(exit_code == 0, f"d50% vi exited with status {exit_code}")
    require(saved == "6\n7\n8\n9\n10\n",
            f"unexpected d50% buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "1\n2\n3\n4\n5\n6\n7\n8\n9\n10\n",
        [b"c", b"5", b"0", b"%", b"X", b"\x1b"],
    )
    require(exit_code == 0, f"c50% vi exited with status {exit_code}")
    require("-- INSERT --" in decoded, "missing c50% insert status")
    require(saved == "X\n6\n7\n8\n9\n10\n",
            f"unexpected c50% buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "1\n2\n3\n4\n5\n6\n7\n8\n9\n10\n",
        [b"y", b"5", b"0", b"%", b"P"],
    )
    require(exit_code == 0, f"y50% vi exited with status {exit_code}")
    require(saved == "1\n2\n3\n4\n5\n1\n2\n3\n4\n5\n6\n7\n8\n9\n10\n",
            f"unexpected y50% buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one two three four\n",
        [b"W", b"W", b"W", b"2", b"g", b"e", b"r", b"Y"],
    )
    require(exit_code == 0, f"ge-motion vi exited with status {exit_code}")
    require(saved == "one twY three four\n",
            f"unexpected ge-motion buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one,two   three\n",
        [b"W", b"g", b"E", b"r", b"Z"],
    )
    require(exit_code == 0, f"gE-motion vi exited with status {exit_code}")
    require(saved == "one,twZ   three\n",
            f"unexpected gE-motion buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one two three four\n",
        [b"W", b"W", b"W", b"d", b"g", b"e"],
    )
    require(exit_code == 0, f"dge vi exited with status {exit_code}")
    require(saved == "one two threour\n",
            f"unexpected dge buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one two three four\n",
        [b"W", b"W", b"W", b"c", b"g", b"e", b"X", b"\x1b"],
    )
    require(exit_code == 0, f"cge vi exited with status {exit_code}")
    require("-- INSERT --" in decoded, "missing cge insert status")
    require(saved == "one two threXour\n",
            f"unexpected cge buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one,two   three\n",
        [b"W", b"d", b"g", b"E"],
    )
    require(exit_code == 0, f"dgE vi exited with status {exit_code}")
    require(saved == "one,twhree\n",
            f"unexpected dgE buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one,two   three\n",
        [b"W", b"c", b"g", b"E", b"X", b"\x1b"],
    )
    require(exit_code == 0, f"cgE vi exited with status {exit_code}")
    require("-- INSERT --" in decoded, "missing cgE insert status")
    require(saved == "one,twXhree\n",
            f"unexpected cgE buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one two three four\n",
        [b"W", b"W", b"W", b"y", b"g", b"e", b"P"],
    )
    require(exit_code == 0, f"yge vi exited with status {exit_code}")
    require(saved == "one two three fe four\n",
            f"unexpected yge buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one,two   three\n",
        [b"W", b"y", b"g", b"E", b"P"],
    )
    require(exit_code == 0, f"ygE vi exited with status {exit_code}")
    require(saved == "one,two   to   three\n",
            f"unexpected ygE buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "alpha beta\n",
        [b"d", b"g", b"e"],
    )
    require(exit_code == 0, f"dge-start vi exited with status {exit_code}")
    require(saved == "alpha beta\n",
            f"unexpected dge-start buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "alpha, beta\n",
        [b"d", b"g", b"E"],
    )
    require(exit_code == 0, f"dgE-start vi exited with status {exit_code}")
    require(saved == "alpha, beta\n",
            f"unexpected dgE-start buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "alpha beta\n",
        [b"c", b"g", b"e", b"X", b"\x1b"],
    )
    require(exit_code == 0, f"cge-start vi exited with status {exit_code}")
    require(saved == "alpha beta\n",
            f"unexpected cge-start buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "alpha, beta\n",
        [b"c", b"g", b"E", b"X", b"\x1b"],
    )
    require(exit_code == 0, f"cgE-start vi exited with status {exit_code}")
    require(saved == "alpha, beta\n",
            f"unexpected cgE-start buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "alpha beta\n",
        [b"y", b"g", b"e", b"P"],
    )
    require(exit_code == 0, f"yge-start vi exited with status {exit_code}")
    require(saved == "alpha beta\n",
            f"unexpected yge-start buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "alpha, beta\n",
        [b"y", b"g", b"E", b"P"],
    )
    require(exit_code == 0, f"ygE-start vi exited with status {exit_code}")
    require(saved == "alpha, beta\n",
            f"unexpected ygE-start buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one two\nalpha beta\n",
        [b"G", b"d", b"b"],
    )
    require(exit_code == 0, f"db-cross vi exited with status {exit_code}")
    require(saved == "one \nalpha beta\n",
            f"unexpected db-cross buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one two\nalpha beta\n",
        [b"G", b"c", b"b", b"X", b"\x1b"],
    )
    require(exit_code == 0, f"cb-cross vi exited with status {exit_code}")
    require("-- INSERT --" in decoded, "missing cb-cross insert status")
    require(saved == "one X\nalpha beta\n",
            f"unexpected cb-cross buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one two\nalpha beta\n",
        [b"G", b"y", b"b", b"P"],
    )
    require(exit_code == 0, f"yb-cross vi exited with status {exit_code}")
    require(saved == "one twotwo\nalpha beta\n",
            f"unexpected yb-cross buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one,two\nalpha,beta\n",
        [b"G", b"d", b"B"],
    )
    require(exit_code == 0, f"dB-cross vi exited with status {exit_code}")
    require(saved == "alpha,beta\n",
            f"unexpected dB-cross buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one,two\nalpha,beta\n",
        [b"G", b"c", b"B", b"X", b"\x1b"],
    )
    require(exit_code == 0, f"cB-cross vi exited with status {exit_code}")
    require("-- INSERT --" in decoded, "missing cB-cross insert status")
    require(saved == "X\nalpha,beta\n",
            f"unexpected cB-cross buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one,two\nalpha,beta\n",
        [b"G", b"y", b"B", b"P"],
    )
    require(exit_code == 0, f"yB-cross vi exited with status {exit_code}")
    require(saved == "one,two\none,two\nalpha,beta\n",
            f"unexpected yB-cross buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one two\nalpha beta\n",
        [b"G", b"$", b"d", b"g", b"e"],
    )
    require(exit_code == 0, f"dge-cross vi exited with status {exit_code}")
    require(saved == "one two\nalph\n",
            f"unexpected dge-cross buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one two\nalpha beta\n",
        [b"G", b"$", b"c", b"g", b"e", b"X", b"\x1b"],
    )
    require(exit_code == 0, f"cge-cross vi exited with status {exit_code}")
    require("-- INSERT --" in decoded, "missing cge-cross insert status")
    require(saved == "one two\nalphX\n",
            f"unexpected cge-cross buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one two\nalpha beta\n",
        [b"G", b"$", b"y", b"g", b"e", b"P"],
    )
    require(exit_code == 0, f"yge-cross vi exited with status {exit_code}")
    require(saved == "one two\nalpha betaa beta\n",
            f"unexpected yge-cross buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one,two\nalpha,beta\n",
        [b"G", b"$", b"d", b"g", b"E"],
    )
    require(exit_code == 0, f"dgE-cross vi exited with status {exit_code}")
    require(saved == "one,tw\n",
            f"unexpected dgE-cross buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one,two\nalpha,beta\n",
        [b"G", b"$", b"c", b"g", b"E", b"X", b"\x1b"],
    )
    require(exit_code == 0, f"cgE-cross vi exited with status {exit_code}")
    require("-- INSERT --" in decoded, "missing cgE-cross insert status")
    require(saved == "one,twX\n",
            f"unexpected cgE-cross buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one,two\nalpha,beta\n",
        [b"G", b"$", b"y", b"g", b"E", b"P"],
    )
    require(exit_code == 0, f"ygE-cross vi exited with status {exit_code}")
    require(saved == "one,two\nalpha,betao\nalpha,beta\n",
            f"unexpected ygE-cross buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "abc def ghi\n",
        [b"0", b"d", b"f", b"g"],
    )
    require(exit_code == 0, f"0dfg vi exited with status {exit_code}")
    require(saved == "hi\n",
            f"unexpected 0dfg buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "abc def ghi\n",
        [b"0", b"c", b"t", b"g", b"X", b"\x1b"],
    )
    require(exit_code == 0, f"0ctg vi exited with status {exit_code}")
    require("-- INSERT --" in decoded, "missing 0ctg insert status")
    require(saved == "Xghi\n",
            f"unexpected 0ctg buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "abc def ghi\n",
        [b"0", b"y", b"f", b"g", b"P"],
    )
    require(exit_code == 0, f"0yfgP vi exited with status {exit_code}")
    require(saved == "abc def gabc def ghi\n",
            f"unexpected 0yfgP buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "abc def ghi\n",
        [b"$", b"y", b"F", b"g", b"P"],
    )
    require(exit_code == 0, f"$yFgP vi exited with status {exit_code}")
    require(saved == "abc def ghghi\n",
            f"unexpected $yFgP buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "abc def ghi\n",
        [b"$", b"y", b"T", b"g", b"P"],
    )
    require(exit_code == 0, f"$yTgP vi exited with status {exit_code}")
    require(saved == "abc def ghhi\n",
            f"unexpected $yTgP buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "abc def ghi\n",
        [b"$", b"d", b"F", b"g"],
    )
    require(exit_code == 0, f"$dFg vi exited with status {exit_code}")
    require(saved == "abc def i\n",
            f"unexpected $dFg buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "abc def ghi\n",
        [b"$", b"c", b"T", b"g", b"X", b"\x1b"],
    )
    require(exit_code == 0, f"$cTg vi exited with status {exit_code}")
    require("-- INSERT --" in decoded, "missing $cTg insert status")
    require(saved == "abc def gXi\n",
            f"unexpected $cTg buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "abcXdefXghiXj\n",
        [b"$", b"c", b"T", b"X", b"T", b"A", b"I", b"L", b"\x1b"],
    )
    require(exit_code == 0, f"$cTX vi exited with status {exit_code}")
    require("-- INSERT --" in decoded, "missing $cTX insert status")
    require(saved == "abcXdefXghiXTAIL\n",
            f"unexpected $cTX buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\n\n  two\nthree\n\nfour\n",
        [b"}", b"}", b"{"],
    )
    require(exit_code == 0, f"paragraph-motion vi exited with status {exit_code}")
    require(decoded.count("line 2/6") >= 2, "missing paragraph backward/forward status")
    require("line 5/6" in decoded, "missing paragraph forward-to-end status")
    require(saved == "one\n\n  two\nthree\n\nfour\n",
            f"unexpected paragraph-motion buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\n\ntwo\nthree\n\nfour\n",
        [b"j", b">", b"}"],
    )
    require(exit_code == 0, f"blank-paragraph-shift vi exited with status {exit_code}")
    require(saved == "one\n\n\ttwo\n\tthree\n\nfour\n",
            f"unexpected blank-paragraph-shift buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\n\ntwo\nthree\n\nfour\n",
        [b"j", b"d", b"}"],
    )
    require(exit_code == 0, f"blank-paragraph-delete-separator vi exited with status {exit_code}")
    require(saved == "one\n\nfour\n",
            f"unexpected blank-paragraph-delete-separator buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\n\ntwo\nthree\n\nfour\n",
        [b"j", b"c", b"}", b"X", b"\x1b"],
    )
    require(exit_code == 0, f"blank-paragraph-change-separator vi exited with status {exit_code}")
    require(saved == "one\nX\n\nfour\n",
            f"unexpected blank-paragraph-change-separator buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\n\ntwo\nthree\n\nfour\n",
        [b"j", b"y", b"}", b"P"],
    )
    require(exit_code == 0, f"blank-paragraph-yank-separator vi exited with status {exit_code}")
    require(saved == "one\n\ntwo\nthree\n\ntwo\nthree\n\nfour\n",
            f"unexpected blank-paragraph-yank-separator buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "head\n{\na\n}\nmid\n{\nb\n}\ntail\n",
        [b"]", b"]", b"r", b"1", b"]", b"]", b"r", b"2"],
    )
    require(exit_code == 0, f"section-start-motion vi exited with status {exit_code}")
    require("line 2/9" in decoded, "missing first section-start status")
    require("line 6/9" in decoded, "missing second section-start status")
    require(saved == "head\n1\na\n}\nmid\n2\nb\n}\ntail\n",
            f"unexpected section-start-motion buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "head\n{\na\n}\nmid\n{\nb\n}\ntail\n",
        [b"]", b"[", b"r", b"3", b"]", b"[", b"r", b"4"],
    )
    require(exit_code == 0, f"section-end-motion vi exited with status {exit_code}")
    require("line 4/9" in decoded, "missing first section-end status")
    require("line 8/9" in decoded, "missing second section-end status")
    require(saved == "head\n{\na\n3\nmid\n{\nb\n4\ntail\n",
            f"unexpected section-end-motion buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "head\n{\na\n}\nmid\n{\nb\n}\ntail\n",
        [b"G", b"[", b"[", b"r", b"5", b"[", b"]", b"r", b"6"],
    )
    require(exit_code == 0, f"section-backward-motion vi exited with status {exit_code}")
    require("line 6/9" in decoded, "missing backward section-start status")
    require("line 4/9" in decoded, "missing backward section-end status")
    require(saved == "head\n{\na\n6\nmid\n5\nb\n}\ntail\n",
            f"unexpected section-backward-motion buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "head\n{\na\n}\nmid\n{\nb\n}\ntail\n",
        [b"d", b"]", b"]"],
    )
    require(exit_code == 0, f"section-delete-forward vi exited with status {exit_code}")
    require(saved == "{\na\n}\nmid\n{\nb\n}\ntail\n",
            f"unexpected section-delete-forward buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "head\n{\na\n}\nmid\n{\nb\n}\ntail\n",
        [b"y", b"]", b"]", b"P"],
    )
    require(exit_code == 0, f"section-yank-forward vi exited with status {exit_code}")
    require(saved == "head\nhead\n{\na\n}\nmid\n{\nb\n}\ntail\n",
            f"unexpected section-yank-forward buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "head\n{\na\n}\nmid\n{\nb\n}\ntail\n",
        [b"c", b"]", b"[", b"D", b"O", b"N", b"E", b"\x1b"],
    )
    require(exit_code == 0, f"section-change-forward vi exited with status {exit_code}")
    require(saved == "DONE\n}\nmid\n{\nb\n}\ntail\n",
            f"unexpected section-change-forward buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "head\n{\na\n}\nmid\n{\nb\n}\ntail\n",
        [b"G", b"d", b"[", b"["],
    )
    require(exit_code == 0, f"section-delete-backward vi exited with status {exit_code}")
    require(saved == "head\n{\na\n}\nmid\ntail\n",
            f"unexpected section-delete-backward buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "head\n{\na\n}\nmid\n{\nb\n}\ntail\n",
        [b"G", b"c", b"[", b"]", b"X", b"\x1b"],
    )
    require(exit_code == 0, f"section-change-backward vi exited with status {exit_code}")
    require(saved == "head\n{\na\n}\nmid\n{\nb\nX\ntail\n",
            f"unexpected section-change-backward buffer: {saved!r}")

    section_operator_cases = [
        ("delete-section-start-forward", "one\n{\ntwo\n}\nthree\n{\nfour\n}\n",
         [b"d", b"]", b"]"], "{\ntwo\n}\nthree\n{\nfour\n}\n", 0),
        ("change-section-start-forward", "one\n{\ntwo\n}\nthree\n{\nfour\n}\n",
         [b"c", b"]", b"]", b"X", b"\x1b"], "X\n{\ntwo\n}\nthree\n{\nfour\n}\n", 1),
        ("yank-section-start-forward", "one\n{\ntwo\n}\nthree\n{\nfour\n}\n",
         [b"y", b"]", b"]", b"P"], "one\none\n{\ntwo\n}\nthree\n{\nfour\n}\n", 0),
        ("shift-section-start-forward", "one\n{\ntwo\n}\nthree\n{\nfour\n}\n",
         [b">", b"]", b"]"], "\tone\n{\ntwo\n}\nthree\n{\nfour\n}\n", 0),
        ("unshift-section-start-forward", "\tone\n\t{\n\ttwo\n\t}\n\tthree\n\t{\n\tfour\n\t}\n",
         [b"<", b"]", b"]"], "one\n{\ntwo\n}\nthree\n{\nfour\n}\n", 0),
        ("delete-section-start-backward", "one\n{\ntwo\n}\nthree\n{\nfour\n}\n",
         [b"G", b"d", b"[", b"["], "one\n{\ntwo\n}\nthree\n}\n", 0),
        ("change-section-start-backward", "one\n{\ntwo\n}\nthree\n{\nfour\n}\n",
         [b"G", b"c", b"[", b"[", b"X", b"\x1b"], "one\n{\ntwo\n}\nthree\nX\n}\n", 1),
        ("yank-section-start-backward", "one\n{\ntwo\n}\nthree\n{\nfour\n}\n",
         [b"G", b"y", b"[", b"[", b"P"], "one\n{\ntwo\n}\nthree\n{\nfour\n{\nfour\n}\n", 0),
        ("shift-section-start-backward", "one\n{\ntwo\n}\nthree\n{\nfour\n}\n",
         [b"G", b">", b"[", b"["], "one\n{\ntwo\n}\nthree\n\t{\n\tfour\n}\n", 0),
        ("unshift-section-start-backward", "\tone\n\t{\n\ttwo\n\t}\n\tthree\n\t{\n\tfour\n\t}\n",
         [b"G", b"<", b"[", b"["], "one\n{\ntwo\n}\nthree\n{\nfour\n}\n", 0),
        ("delete-section-end-forward", "one\n{\ntwo\n}\nthree\n{\nfour\n}\n",
         [b"d", b"]", b"["], "}\nthree\n{\nfour\n}\n", 0),
        ("change-section-end-forward", "one\n{\ntwo\n}\nthree\n{\nfour\n}\n",
         [b"c", b"]", b"[", b"X", b"\x1b"], "X\n}\nthree\n{\nfour\n}\n", 1),
        ("yank-section-end-forward", "one\n{\ntwo\n}\nthree\n{\nfour\n}\n",
         [b"y", b"]", b"[", b"P"], "one\n{\ntwo\none\n{\ntwo\n}\nthree\n{\nfour\n}\n", 0),
        ("shift-section-end-forward", "one\n{\ntwo\n}\nthree\n{\nfour\n}\n",
         [b">", b"]", b"["], "\tone\n\t{\n\ttwo\n}\nthree\n{\nfour\n}\n", 0),
        ("unshift-section-end-forward", "\tone\n\t{\n\ttwo\n\t}\n\tthree\n\t{\n\tfour\n\t}\n",
         [b"<", b"]", b"["], "one\n{\ntwo\n}\nthree\n{\nfour\n\t}\n", 0),
        ("delete-section-end-backward", "one\n{\ntwo\n}\nthree\n{\nfour\n}\n",
         [b"G", b"d", b"[", b"]"], "one\n{\ntwo\n}\n", 0),
        ("change-section-end-backward", "one\n{\ntwo\n}\nthree\n{\nfour\n}\n",
         [b"G", b"c", b"[", b"]", b"X", b"\x1b"], "one\n{\ntwo\nX\n}\n", 1),
        ("yank-section-end-backward", "one\n{\ntwo\n}\nthree\n{\nfour\n}\n",
         [b"G", b"y", b"[", b"]", b"P"], "one\n{\ntwo\n}\nthree\n{\nfour\n}\nthree\n{\nfour\n}\n", 0),
        ("shift-section-end-backward", "one\n{\ntwo\n}\nthree\n{\nfour\n}\n",
         [b"G", b">", b"[", b"]"], "one\n{\ntwo\n\t}\n\tthree\n\t{\n\tfour\n}\n", 0),
        ("unshift-section-end-backward", "\tone\n\t{\n\ttwo\n\t}\n\tthree\n\t{\n\tfour\n\t}\n",
         [b"G", b"<", b"[", b"]"], "one\n{\ntwo\n}\nthree\n{\nfour\n}\n", 0),
    ]
    for name, initial_text, keys, expected, want_insert in section_operator_cases:
        exit_code, decoded, saved = run_vi_session(vi_path, initial_text, keys)
        require(exit_code == 0, f"{name} vi exited with status {exit_code}")
        if want_insert:
            require("-- INSERT --" in decoded, f"missing {name} insert status")
        require(saved == expected, f"unexpected {name} buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "Alpha one.  Beta two!\nGamma three?  Delta four.\n",
        [b")", b"r", b"1", b")", b"r", b"2"],
    )
    require(exit_code == 0, f"sentence-forward vi exited with status {exit_code}")
    require("line 1/2" in decoded, "missing sentence-forward line 1 status")
    require("line 2/2" in decoded, "missing sentence-forward line 2 status")
    require(saved == "Alpha one.  1eta two!\n2amma three?  Delta four.\n",
            f"unexpected sentence-forward buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one. two. three.\nalpha. beta. gamma.\n",
        [b"$", b")", b"r", b"Z"],
    )
    require(exit_code == 0, f"sentence-forward-line-end vi exited with status {exit_code}")
    require(saved == "one. two. three.\nZlpha. beta. gamma.\n",
            f"unexpected sentence-forward-line-end buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "Alpha one.  Beta two!\nGamma three?  Delta four.\n",
        [b"G", b"$", b"(", b"r", b"3", b"(", b"r", b"4"],
    )
    require(exit_code == 0, f"sentence-backward vi exited with status {exit_code}")
    require(decoded.count("line 2/2") >= 1, "missing sentence-backward line 2 status")
    require(decoded.count("line 1/2") >= 1, "missing sentence-backward line 1 status")
    require(saved == "Alpha one.  Beta two!\n4amma three?  3elta four.\n",
            f"unexpected sentence-backward buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\n\n two\nthree\n\n\nend\n",
        [b"w", b"r", b"X"],
    )
    require(exit_code == 0, f"blank-word-forward vi exited with status {exit_code}")
    require(saved == "one\n\n two\nthree\n\n\nend\n",
            f"unexpected blank-word-forward buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\n\n two\nthree\n\n\nend\n",
        [b"2", b"w", b"r", b"X"],
    )
    require(exit_code == 0, f"blank-word-forward-count vi exited with status {exit_code}")
    require(saved == "one\n\n Xwo\nthree\n\n\nend\n",
            f"unexpected blank-word-forward-count buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\n\n two\nthree\n\n\nend\n",
        [b"2", b"e", b"r", b"X"],
    )
    require(exit_code == 0, f"blank-word-end-count vi exited with status {exit_code}")
    require(saved == "one\n\n twX\nthree\n\n\nend\n",
            f"unexpected blank-word-end-count buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\n\n two\nthree\n\n\nend\n",
        [b"G", b"b", b"r", b"X"],
    )
    require(exit_code == 0, f"blank-word-backward vi exited with status {exit_code}")
    require(saved == "one\n\n two\nthree\n\n\nend\n",
            f"unexpected blank-word-backward buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\n\n two\nthree\n\n\nend\n",
        [b"G", b"B", b"r", b"X"],
    )
    require(exit_code == 0, f"blank-bigword-backward vi exited with status {exit_code}")
    require(saved == "one\n\n two\nthree\n\n\nend\n",
            f"unexpected blank-bigword-backward buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\n\n two\nthree\n\n\nend\n",
        [b"W", b"r", b"X"],
    )
    require(exit_code == 0, f"blank-bigword-forward vi exited with status {exit_code}")
    require(saved == "one\n\n two\nthree\n\n\nend\n",
            f"unexpected blank-bigword-forward buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one two three four five six\n\nalpha beta gamma delta\n\nsec\n{\nbody\n}\n",
        [b"}", b"r", b"X"],
    )
    require(exit_code == 0, f"blank-paragraph-forward vi exited with status {exit_code}")
    require(saved == "one two three four five six\n\nalpha beta gamma delta\n\nsec\n{\nbody\n}\n",
            f"unexpected blank-paragraph-forward buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one two three four five six\n\nalpha beta gamma delta\n\nsec\n{\nbody\n}\n",
        [b"2", b"}", b"r", b"X"],
    )
    require(exit_code == 0, f"blank-paragraph-forward-count vi exited with status {exit_code}")
    require(saved == "one two three four five six\n\nalpha beta gamma delta\n\nsec\n{\nbody\n}\n",
            f"unexpected blank-paragraph-forward-count buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one two three four five six\n\nalpha beta gamma delta\n\nsec\n{\nbody\n}\n",
        [b"d", b"}"],
    )
    require(exit_code == 0, f"blank-paragraph-delete vi exited with status {exit_code}")
    require(saved == "\nalpha beta gamma delta\n\nsec\n{\nbody\n}\n",
            f"unexpected blank-paragraph-delete buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one two three four five six\n\nalpha beta gamma delta\n\nsec\n{\nbody\n}\n",
        [b"2", b"d", b"}"],
    )
    require(exit_code == 0, f"blank-paragraph-delete-count vi exited with status {exit_code}")
    require(saved == "\nsec\n{\nbody\n}\n",
            f"unexpected blank-paragraph-delete-count buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one two three four five six\n\nalpha beta gamma delta\n\nsec\n{\nbody\n}\n",
        [b"c", b"}", b"X", b"\x1b"],
    )
    require(exit_code == 0, f"blank-paragraph-change vi exited with status {exit_code}")
    require(saved == "X\n\nalpha beta gamma delta\n\nsec\n{\nbody\n}\n",
            f"unexpected blank-paragraph-change buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one two three four five six\n\nalpha beta gamma delta\n\nsec\n{\nbody\n}\n",
        [b"y", b"}", b"P"],
    )
    require(exit_code == 0, f"blank-paragraph-yank vi exited with status {exit_code}")
    require(saved == "one two three four five six\none two three four five six\n\nalpha beta gamma delta\n\nsec\n{\nbody\n}\n",
            f"unexpected blank-paragraph-yank buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one two three four five six\n\nalpha beta gamma delta\n\nsec\n{\nbody\n}\n",
        [b")", b"r", b"X"],
    )
    require(exit_code == 0, f"blank-sentence-forward vi exited with status {exit_code}")
    require(saved == "one two three four five six\n\nalpha beta gamma delta\n\nsec\n{\nbody\n}\n",
            f"unexpected blank-sentence-forward buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one two three four five six\n\nalpha beta gamma delta\n\nsec\n{\nbody\n}\n",
        [b"2", b")", b"r", b"X"],
    )
    require(exit_code == 0, f"blank-sentence-forward-count vi exited with status {exit_code}")
    require(saved == "one two three four five six\n\nXlpha beta gamma delta\n\nsec\n{\nbody\n}\n",
            f"unexpected blank-sentence-forward-count buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one two three four five six\n\nalpha beta gamma delta\n\nsec\n{\nbody\n}\n",
        [b"d", b")"],
    )
    require(exit_code == 0, f"blank-sentence-delete vi exited with status {exit_code}")
    require(saved == "\nalpha beta gamma delta\n\nsec\n{\nbody\n}\n",
            f"unexpected blank-sentence-delete buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one two three four five six\n\nalpha beta gamma delta\n\nsec\n{\nbody\n}\n",
        [b"2", b"d", b")"],
    )
    require(exit_code == 0, f"blank-sentence-delete-count vi exited with status {exit_code}")
    require(saved == "alpha beta gamma delta\n\nsec\n{\nbody\n}\n",
            f"unexpected blank-sentence-delete-count buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one two three four five six\n\nalpha beta gamma delta\n\nsec\n{\nbody\n}\n",
        [b"y", b")", b"P"],
    )
    require(exit_code == 0, f"blank-sentence-yank vi exited with status {exit_code}")
    require(saved == "one two three four five six\none two three four five six\n\nalpha beta gamma delta\n\nsec\n{\nbody\n}\n",
            f"unexpected blank-sentence-yank buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one two three four five six\n\nalpha beta gamma delta\n\nsec\n{\nbody\n}\n",
        [b"2", b"y", b")", b"P"],
    )
    require(exit_code == 0, f"blank-sentence-yank-count vi exited with status {exit_code}")
    require(saved == "one two three four five six\n\none two three four five six\n\nalpha beta gamma delta\n\nsec\n{\nbody\n}\n",
            f"unexpected blank-sentence-yank-count buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one two.\n\nalpha beta. gamma delta.\n\nsec\n{\nbody\n}\n",
        [b"c", b")", b"X", b"\x1b"],
    )
    require(exit_code == 0, f"blank-sentence-change-separator vi exited with status {exit_code}")
    require(saved == "X\n\nalpha beta. gamma delta.\n\nsec\n{\nbody\n}\n",
            f"unexpected blank-sentence-change-separator buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "\nalpha beta.\n",
        [b"d", b")"],
    )
    require(exit_code == 0, f"blank-sentence-delete-emptyline vi exited with status {exit_code}")
    require(saved == "alpha beta.\n",
            f"unexpected blank-sentence-delete-emptyline buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one two three four five six\n\nalpha beta gamma delta\n\nsec\n{\nbody\n}\n",
        [b"G", b"(", b"r", b"X"],
    )
    require(exit_code == 0, f"blank-sentence-backward vi exited with status {exit_code}")
    require(saved == "one two three four five six\n\nalpha beta gamma delta\n\nXec\n{\nbody\n}\n",
            f"unexpected blank-sentence-backward buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one two three four five six\n\nalpha beta gamma delta\n\nsec\n{\nbody\n}\n",
        [b"G", b"2", b"(", b"r", b"X"],
    )
    require(exit_code == 0, f"blank-sentence-backward-count vi exited with status {exit_code}")
    require(saved == "one two three four five six\n\nalpha beta gamma delta\n\nsec\n{\nbody\n}\n",
            f"unexpected blank-sentence-backward-count buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one two three four five six\n\nalpha beta gamma delta\n\nsec\n{\nbody\n}\n",
        [b"G", b"d", b"("],
    )
    require(exit_code == 0, f"blank-sentence-backward-delete vi exited with status {exit_code}")
    require(saved == "one two three four five six\n\nalpha beta gamma delta\n\n}\n",
            f"unexpected blank-sentence-backward-delete buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one two three four five six\n\nalpha beta gamma delta\n\nsec\n{\nbody\n}\n",
        [b"G", b"2", b"d", b"("],
    )
    require(exit_code == 0, f"blank-sentence-backward-delete-count vi exited with status {exit_code}")
    require(saved == "one two three four five six\n\nalpha beta gamma delta\n}\n",
            f"unexpected blank-sentence-backward-delete-count buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one two three four five six\n\nalpha beta gamma delta\n\nsec\n{\nbody\n}\n",
        [b"G", b"c", b"(", b"X", b"\x1b"],
    )
    require(exit_code == 0, f"blank-sentence-backward-change vi exited with status {exit_code}")
    require(saved == "one two three four five six\n\nalpha beta gamma delta\n\nX\n}\n",
            f"unexpected blank-sentence-backward-change buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one two three four five six\n\nalpha beta gamma delta\n\nsec\n{\nbody\n}\n",
        [b"G", b"y", b"(", b"P"],
    )
    require(exit_code == 0, f"blank-sentence-backward-yank vi exited with status {exit_code}")
    require(saved == "one two three four five six\n\nalpha beta gamma delta\n\nsec\n{\nbody\nsec\n{\nbody\n}\n",
            f"unexpected blank-sentence-backward-yank buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one two three four five six\n\nalpha beta gamma delta\n\nsec\n{\nbody\n}\n",
        [b"G", b"y", b"{", b"P"],
    )
    require(exit_code == 0, f"blank-paragraph-backward-yank vi exited with status {exit_code}")
    require(saved == "one two three four five six\n\nalpha beta gamma delta\n\nsec\n{\nbody\n\nsec\n{\nbody\n}\n",
            f"unexpected blank-paragraph-backward-yank buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "\nalpha beta gamma delta\n",
        [b"d", b"}"],
    )
    require(exit_code == 0, f"blank-paragraph-delete-emptyline vi exited with status {exit_code}")
    require(saved == "",
            f"unexpected blank-paragraph-delete-emptyline buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "abcde\n",
        [b"\x1b[F", b"r", b"Z", b"\x1b[H", b"r", b"Y"],
    )
    require(exit_code == 0, f"home-end-motion vi exited with status {exit_code}")
    require(saved == "YbcdZ\n",
            f"unexpected home-end-motion buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "".join(f"line {i}\n" for i in range(1, 41)),
        [b"1", b"0", b"G", b"\x04", b"\x15"],
    )
    require(exit_code == 0, f"scroll vi exited with status {exit_code}")
    require("line 10/40" in decoded, "missing initial half-page anchor")
    require("line 21/40" in decoded, "missing half-page down status")
    require(saved.startswith("line 1\nline 2\nline 3\n"),
            "unexpected half-page scroll buffer contents")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "".join(f"line {i}\n" for i in range(1, 9)),
        [b"j", b"\x05", b"r", b"Z"],
        rows=5,
        cols=20,
    )
    require(exit_code == 0, f"line-scroll-down vi exited with status {exit_code}")
    require(saved == "line 1\nline 2\nZine 3\nline 4\nline 5\nline 6\nline 7\nline 8\n",
            f"unexpected line-scroll-down buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "".join(f"line {i}\n" for i in range(1, 9)),
        [b"j", b"2", b"\x05", b"r", b"Z"],
        rows=5,
        cols=20,
    )
    require(exit_code == 0, f"counted-line-scroll-down vi exited with status {exit_code}")
    require(saved == "line 1\nline 2\nline 3\nZine 4\nline 5\nline 6\nline 7\nline 8\n",
            f"unexpected counted line-scroll-down buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "".join(f"line {i}\n" for i in range(1, 9)),
        [b"5", b"G", b"z", b"\r", b"\x19", b"r", b"Z"],
        rows=5,
        cols=20,
    )
    require(exit_code == 0, f"line-scroll-up vi exited with status {exit_code}")
    require(saved == "line 1\nline 2\nline 3\nline 4\nZine 5\nline 6\nline 7\nline 8\n",
            f"unexpected line-scroll-up buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "".join(f"line {i}\n" for i in range(1, 9)),
        [b"5", b"G", b"z", b"\r", b"2", b"\x19", b"r", b"Z"],
        rows=5,
        cols=20,
    )
    require(exit_code == 0, f"counted-line-scroll-up vi exited with status {exit_code}")
    require(saved == "line 1\nline 2\nline 3\nline 4\nZine 5\nline 6\nline 7\nline 8\n",
            f"unexpected counted line-scroll-up buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "".join(f"line {i}\n" for i in range(1, 41)),
        [b"\x1b[6~", b"r", b"X", b"\x1b[5~", b"r", b"Y"],
    )
    require(exit_code == 0, f"page-key-scroll vi exited with status {exit_code}")
    require("line 23/40" in decoded, "missing page-down key status")
    require("line 1/40" in decoded, "missing page-up key status")
    require(saved.startswith("Yine 1\nline 2\nline 3\n"),
            "unexpected page-key-scroll buffer contents")
    require("Xine 23\n" in saved, "missing page-down key edit target")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "".join(f"line {i}\n" for i in range(1, 41)),
        [b"2", b"0", b"G", b"z", b"z", b"M", b"z", b"\r", b"H", b"z", b"-", b"L"],
    )
    require(exit_code == 0, f"z-position vi exited with status {exit_code}")
    require(decoded.count("line 20/40") >= 4, "missing z-position status transitions")
    require(saved.startswith("line 1\nline 2\nline 3\n"),
            "unexpected z-position buffer contents")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "".join(
            "   abcdef\n" if i == 20 else f"line {i}\n"
            for i in range(1, 41)
        ),
        [b"2", b"0", b"G", b"5", b"|", b"z", b".", b"r", b"X"],
    )
    require(exit_code == 0, f"z-dot vi exited with status {exit_code}")
    require(saved.splitlines()[19] == "   Xbcdef",
            f"unexpected z-dot cursor placement: {saved.splitlines()[19]!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "".join(
            "   abcdef\n" if i == 20 else f"line {i}\n"
            for i in range(1, 41)
        ),
        [b"2", b"0", b"G", b"5", b"|", b"z", b"z", b"r", b"Y"],
    )
    require(exit_code == 0, f"zz vi exited with status {exit_code}")
    require(saved.splitlines()[19] == "   aYcdef",
            f"unexpected zz cursor placement: {saved.splitlines()[19]!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "".join(
            "   abcdef\n" if i == 20 else f"line {i}\n"
            for i in range(1, 41)
        ),
        [b"2", b"0", b"G", b"5", b"|", b"z", b"t", b"r", b"T"],
    )
    require(exit_code == 0, f"zt vi exited with status {exit_code}")
    require(saved.splitlines()[19] == "   aTcdef",
            f"unexpected zt cursor placement: {saved.splitlines()[19]!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "".join(
            "   abcdef\n" if i == 20 else f"line {i}\n"
            for i in range(1, 41)
        ),
        [b"2", b"0", b"G", b"5", b"|", b"z", b"b", b"r", b"B"],
    )
    require(exit_code == 0, f"zb vi exited with status {exit_code}")
    require(saved.splitlines()[19] == "   aBcdef",
            f"unexpected zb cursor placement: {saved.splitlines()[19]!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "".join(f"line {i}\n" for i in range(1, 101)),
        [b"2", b"0", b"G", b"z", b"\r", b"z", b"+", b"r", b"X", b"z", b"^", b"r", b"Y"],
    )
    require(exit_code == 0, f"z-plus-caret vi exited with status {exit_code}")
    require("line 43/100" in decoded, "missing z+ status")
    require("line 42/100" in decoded, "missing z^ status")
    require("Yine 42" in saved, "missing z^ edit target")
    require("Xine 43" in saved, "missing z+ edit target")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "aa\nmatch one\nbb\nmatch two\ncc\nmatch three\n",
        [b"/", b"match\r", b"2", b"n", b"2", b"N"],
    )
    require(exit_code == 0, f"search-repeat vi exited with status {exit_code}")
    require("line 2/6" in decoded, "missing initial search status")
    require("line 6/6" in decoded, "missing counted n status")
    require(saved == "aa\nmatch one\nbb\nmatch two\ncc\nmatch three\n",
            "unexpected counted search buffer contents")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "alpha\nbeta\nalpha two\nbeta again\nalpha three\n",
        [b"*", b"n", b"#"],
    )
    require(exit_code == 0, f"word-search vi exited with status {exit_code}")
    require(decoded.count("line 3/5") >= 1, "missing star-search status")
    require(decoded.count("line 5/5") >= 1, "missing star repeat status")
    require(decoded.count("line 3/5") >= 2, "missing hash-search status")
    require(saved == "alpha\nbeta\nalpha two\nbeta again\nalpha three\n",
            "unexpected word-search buffer contents")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "if (alpha[beta{gamma}delta]omega)\nnext line\n",
        [b"f", b"(", b"%", b"r", b"1", b"0", b"f", b"]", b"%", b"r", b"2"],
    )
    require(exit_code == 0, f"match-motion vi exited with status {exit_code}")
    require("line 1/2" in decoded, "missing match-motion status")
    require(saved == "if (alpha2beta{gamma}delta]omega1\nnext line\n",
            f"unexpected match-motion buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "if (alpha[beta{gamma}delta]omega)\n",
        [b"0", b"f", b"(", b"d", b"%"],
    )
    require(exit_code == 0, f"match-delete vi exited with status {exit_code}")
    require(saved == "if \n",
            f"unexpected match-delete buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "if (alpha[beta{gamma}delta]omega)\n",
        [b"0", b"f", b"(", b"c", b"%", b"X", b"\x1b"],
    )
    require(exit_code == 0, f"match-change vi exited with status {exit_code}")
    require("-- INSERT --" in decoded, "missing match-change insert status")
    require(saved == "if X\n",
            f"unexpected match-change buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "if (alpha[beta{gamma}delta]omega)\n",
        [b"0", b"f", b"(", b"y", b"%", b"P"],
    )
    require(exit_code == 0, f"match-yank vi exited with status {exit_code}")
    require(saved == "if (alpha[beta{gamma}delta]omega)(alpha[beta{gamma}delta]omega)\n",
            f"unexpected match-yank buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "".join(f"line {i}\n" for i in range(1, 11)),
        [b"5", b"0", b"%"],
    )
    require(exit_code == 0, f"percent-goto vi exited with status {exit_code}")
    require("line 5/10" in decoded, "missing percent-goto status")
    require(saved.startswith("line 1\nline 2\nline 3\n"),
            "unexpected percent-goto buffer contents")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "  one\n\tTwo\nthree\n",
        [b"+", b"\r", b"-"],
    )
    require(exit_code == 0, f"line-motion vi exited with status {exit_code}")
    require("line 2/3" in decoded, "missing plus-motion status")
    require("line 3/3" in decoded, "missing enter-motion status")
    require(saved == "  one\n\tTwo\nthree\n",
            "unexpected line-motion buffer contents")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "alpha beta gamma\n",
        [b"w", b"w", b"d", b"b", b"u", b"w", b"c", b"b", b"D", b"O", b"N", b"E", b"\x1b"],
    )
    require(exit_code == 0, f"backward-operator vi exited with status {exit_code}")
    require("-- INSERT --" in decoded, "missing backward change insert status")
    require(saved == "alpha DONEgamma\n",
            f"unexpected backward-operator buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "abc def\n",
        [b"c", b"w", b"X", b"\x1b"],
    )
    require(exit_code == 0, f"cw vi exited with status {exit_code}")
    require(saved == "X def\n",
            f"unexpected cw buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "abc def ghi\n",
        [b"c", b"2", b"w", b"X", b"\x1b"],
    )
    require(exit_code == 0, f"c2w vi exited with status {exit_code}")
    require(saved == "X ghi\n",
            f"unexpected c2w buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "abc def\n",
        [b"c", b"W", b"X", b"\x1b"],
    )
    require(exit_code == 0, f"cW vi exited with status {exit_code}")
    require(saved == "X def\n",
            f"unexpected cW buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "abc def ghi\n",
        [b"c", b"2", b"W", b"X", b"\x1b"],
    )
    require(exit_code == 0, f"c2W vi exited with status {exit_code}")
    require(saved == "X ghi\n",
            f"unexpected c2W buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one,two   three\n",
        [b"d", b"W"],
    )
    require(exit_code == 0, f"dW vi exited with status {exit_code}")
    require(saved == "three\n",
            f"unexpected dW buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one two three four five six\n",
        [b"2", b"d", b"2", b"w"],
    )
    require(exit_code == 0, f"2d2w vi exited with status {exit_code}")
    require(saved == "five six\n",
            f"unexpected 2d2w buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one two three four five six\n",
        [b"2", b"c", b"2", b"w", b"X", b"\x1b"],
    )
    require(exit_code == 0, f"2c2w vi exited with status {exit_code}")
    require(saved == "X five six\n",
            f"unexpected 2c2w buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one two three four five six\n",
        [b"2", b"c", b"2", b"W", b"X", b"\x1b"],
    )
    require(exit_code == 0, f"2c2W vi exited with status {exit_code}")
    require(saved == "X five six\n",
            f"unexpected 2c2W buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one two three four five six\n",
        [b"2", b"y", b"2", b"w", b"P"],
    )
    require(exit_code == 0, f"2y2wP vi exited with status {exit_code}")
    require(saved == "one two three four one two three four five six\n",
            f"unexpected 2y2wP buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "a bc\n",
        [b"l", b"c", b"w", b"X", b"\x1b"],
    )
    require(exit_code == 0, f"space-cw vi exited with status {exit_code}")
    require(saved == "aXbc\n",
            f"unexpected space-cw buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "abcd\n",
        [b"$", b"X", b".", b"u", b"$", b"2", b"X"],
    )
    require(exit_code == 0, f"backward-char vi exited with status {exit_code}")
    require("line 1/1" in decoded, "missing backward-char status")
    require(saved == "d\n",
            f"unexpected backward-char buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "  alpha beta\n",
        [b"$", b"d", b"^", b"u", b"$", b"c", b"0", b"D", b"O", b"N", b"E", b"\x1b"],
    )
    require(exit_code == 0, f"line-start-operator vi exited with status {exit_code}")
    require("-- INSERT --" in decoded, "missing line-start change insert status")
    require(saved == "DONEa\n",
            f"unexpected line-start-operator buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "AbCd\n",
        [b"~", b".", b"2", b"~"],
    )
    require(exit_code == 0, f"toggle-case vi exited with status {exit_code}")
    require("line 1/1" in decoded, "missing toggle-case status")
    require(saved == "aBcD\n",
            f"unexpected toggle-case buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "abcaXca\n",
        [b"f", b"a", b";", b",", b"l", b"r", b"1", b"$", b"F", b"a", b"h", b"r", b"2"],
    )
    require(exit_code == 0, f"find-motion vi exited with status {exit_code}")
    require(saved == "ab2a1ca\n",
            f"unexpected find-motion buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "abcXdefXghiXj\n",
        [b"t", b"X", b"l", b";", b",", b"r", b"2", b"$", b"T", b"X", b"r", b"1"],
    )
    require(exit_code == 0, f"till-motion vi exited with status {exit_code}")
    require(saved == "abcX2efXghiX1\n",
            f"unexpected till-motion buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "abcXdefXghiXj\n",
        [
            b"0", b"d", b"f", b"X", b"u",
            b"0", b"d", b"t", b"X", b"u",
            b"$", b"c", b"F", b"X", b"D", b"O", b"N", b"E", b"\x1b", b"u",
            b"$", b"c", b"T", b"X", b"T", b"A", b"I", b"L", b"\x1b",
        ],
    )
    require(exit_code == 0, f"operator-find vi exited with status {exit_code}")
    require("-- INSERT --" in decoded, "missing operator-find insert status")
    require(saved == "abcXdefXghiXTAIL\n",
            f"unexpected operator-find buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "abcXdefXghiXj\n",
        [b"0", b"d", b"f", b"X", b"."],
    )
    require(exit_code == 0, f"repeat-df vi exited with status {exit_code}")
    require(saved == "ghiXj\n",
            f"unexpected repeat-df buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "abcXdefXghiXj\n",
        [b"0", b"d", b"t", b"X", b"."],
    )
    require(exit_code == 0, f"repeat-dt vi exited with status {exit_code}")
    require(saved == "XghiXj\n",
            f"unexpected repeat-dt buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "abcXdefXghiXj\n",
        [b"0", b"f", b"X", b"d", b";"],
    )
    require(exit_code == 0, f"operator-semicolon-delete vi exited with status {exit_code}")
    require(saved == "abcghiXj\n",
            f"unexpected operator-semicolon-delete buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "abcXdefXghiXj\n",
        [b"0", b"f", b"X", b"c", b";", b"T", b"A", b"I", b"L", b"\x1b"],
    )
    require(exit_code == 0, f"operator-semicolon-change vi exited with status {exit_code}")
    require("-- INSERT --" in decoded, "missing operator-semicolon change insert status")
    require(saved == "abcTAILghiXj\n",
            f"unexpected operator-semicolon-change buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "abcXdefXghiXj\n",
        [b"0", b"f", b"X", b"y", b";", b"P"],
    )
    require(exit_code == 0, f"operator-semicolon-yank vi exited with status {exit_code}")
    require(saved == "abcXdefXXdefXghiXj\n",
            f"unexpected operator-semicolon-yank buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "abc def ghi jkl\n",
        [b"f", b"j", b"c", b",", b"X", b"\x1b"],
    )
    require(exit_code == 0, f"operator-comma-change vi exited with status {exit_code}")
    require(saved == "abc def ghijkl\n",
            f"unexpected operator-comma-change buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\n  two\nthree\n",
        [b"j", b"m", b"a", b"G", b"'", b"a", b"r", b"T", b"G", b"`", b"a", b"r", b">"],
    )
    require(exit_code == 0, f"visual-mark vi exited with status {exit_code}")
    require("line 2/3" in decoded, "missing mark jump status")
    require(saved == "one\n> two\nthree\n",
            f"unexpected visual-mark buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\ntwo\nthree\n",
        [b"\x1d", b"\x14", b"A", b"?", b"\x1b"],
        extra_files={"tags": "one\tbuffer.txt\t3\n"},
    )
    require(exit_code == 0, f"visual-tag-stack vi exited with status {exit_code}")
    require("line 3/3" in decoded, "missing Ctrl-] tag jump status")
    require("line 1/3" in decoded, "missing Ctrl-T tag pop status")
    require(saved == "one?\ntwo\nthree\n",
            f"unexpected visual-tag-stack buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\ntwo\nthree\nfour\n",
        [b"G", b"m", b"a", b"g", b"g", b"d", b"'", b"a"],
    )
    require(exit_code == 0, f"visual-mark-delete vi exited with status {exit_code}")
    require(saved == "",
            f"unexpected visual-mark-delete buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\ntwo\nthree\nfour\n",
        [b"g", b"g", b"m", b"a", b"G", b"d", b"'", b"a"],
    )
    require(exit_code == 0, f"visual-mark-delete-upward vi exited with status {exit_code}")
    require(saved == "",
            f"unexpected visual-mark-delete-upward buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\ntwo\nthree\nfour\n",
        [b"G", b"m", b"a", b"g", b"g", b"c", b"'", b"a", b"X", b"\x1b"],
    )
    require(exit_code == 0, f"visual-mark-change vi exited with status {exit_code}")
    require("-- INSERT --" in decoded, "missing visual-mark change insert status")
    require(saved == "X\n",
            f"unexpected visual-mark-change buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\ntwo\nthree\nfour\n",
        [b"c", b"+", b"X", b"\x1b", b"j", b"."],
    )
    require(exit_code == 0, f"visual-dot-after-cplus vi exited with status {exit_code}")
    require(saved == "X\nX\n",
            f"unexpected visual-dot-after-cplus buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\ntwo\nthree\nfour\nfive\n",
        [b"2", b"G", b"c", b"M", b"X", b"\x1b", b"j", b"."],
    )
    require(exit_code == 0, f"visual-dot-after-cM vi exited with status {exit_code}")
    require(saved == "one\nX\nfour\nfive\n",
            f"unexpected visual-dot-after-cM buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\ntwo\nthree\nfour\n",
        [b"j", b"c", b"-", b"X", b"\x1b", b"j", b"."],
    )
    require(exit_code == 0, f"visual-dot-after-cminus vi exited with status {exit_code}")
    require(saved == "X\nfour\n",
            f"unexpected visual-dot-after-cminus buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\ntwo\nthree\n",
        [b"c", b"_", b"X", b"\x1b", b"j", b"."],
    )
    require(exit_code == 0, f"visual-dot-after-cunderscore vi exited with status {exit_code}")
    require(saved == "X\nX\nthree\n",
            f"unexpected visual-dot-after-cunderscore buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "alpha beta gamma\nalpha beta gamma\n",
        [b"$", b"c", b"T", b"b", b"X", b"\x1b", b"j", b"."],
    )
    require(exit_code == 0, f"visual-dot-after-cT vi exited with status {exit_code}")
    require(saved == "alpha bXa\nalpha bXeta gamma\n",
            f"unexpected visual-dot-after-cT buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one two three four\nalpha beta gamma\n",
        [b"$", b"c", b"b", b"X", b"\x1b", b"j", b"."],
    )
    require(exit_code == 0, f"visual-dot-after-cb vi exited with status {exit_code}")
    require(saved == "one two three Xr\nalpha beta Xma\n",
            f"unexpected visual-dot-after-cb buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one two three four\nalpha beta gamma\n",
        [b"$", b"c", b"B", b"X", b"\x1b", b"j", b"."],
    )
    require(exit_code == 0, f"visual-dot-after-cB vi exited with status {exit_code}")
    require(saved == "one two three Xr\nalpha beta Xma\n",
            f"unexpected visual-dot-after-cB buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "alpha beta gamma\nalpha beta gamma\n",
        [b"0", b"c", b"t", b"g", b"X", b"\x1b", b"j", b";", b"."],
    )
    require(exit_code == 0, f"visual-dot-after-csemicolon vi exited with status {exit_code}")
    require(saved == "Xgamma\nalpha betaXgamma\n",
            f"unexpected visual-dot-after-csemicolon buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "alpha beta gamma\nalpha beta gamma\n",
        [b"$", b"c", b"T", b"b", b"X", b"\x1b", b"j", b",", b"."],
    )
    require(exit_code == 0, f"visual-dot-after-ccomma vi exited with status {exit_code}")
    require(saved == "alpha bXa\nalpha bXeta gamma\n",
            f"unexpected visual-dot-after-ccomma buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one two three four\nalpha beta gamma\n",
        [b"$", b"c", b"g", b"e", b"X", b"\x1b", b"j", b"."],
    )
    require(exit_code == 0, f"visual-dot-after-cge vi exited with status {exit_code}")
    require(saved == "one two threX\nalpha betXmma\n",
            f"unexpected visual-dot-after-cge buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one two three four\nalpha beta gamma\n",
        [b"$", b"c", b"g", b"E", b"X", b"\x1b", b"j", b"."],
    )
    require(exit_code == 0, f"visual-dot-after-cgE vi exited with status {exit_code}")
    require(saved == "one two threX\nalpha betXmma\n",
            f"unexpected visual-dot-after-cgE buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\ntwo\nthree\nfour\n",
        [b"G", b"m", b"a", b"g", b"g", b"y", b"'", b"a", b"P"],
    )
    require(exit_code == 0, f"visual-mark-yank vi exited with status {exit_code}")
    require(saved == "one\ntwo\nthree\nfour\none\ntwo\nthree\nfour\n",
            f"unexpected visual-mark-yank buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "zero\none\ntwo\n",
        [b"0", b"j", b"m", b"a", b"k", b">", b"'", b"a"],
    )
    require(exit_code == 0, f"visual-mark-shift vi exited with status {exit_code}")
    require(saved == "\tzero\n\tone\ntwo\n",
            f"unexpected visual-mark-shift buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "\tzero\n\tone\ntwo\n",
        [b"0", b"j", b"m", b"a", b"k", b"<", b"'", b"a"],
    )
    require(exit_code == 0, f"visual-mark-unshift vi exited with status {exit_code}")
    require(saved == "zero\none\ntwo\n",
            f"unexpected visual-mark-unshift buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "One. Two. Three.\n",
        [b">", b")"],
    )
    require(exit_code == 0, f"visual-sentence-shift vi exited with status {exit_code}")
    require(saved == "\tOne. Two. Three.\n",
            f"unexpected visual-sentence-shift buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "\tOne. Two. Three.\n",
        [b"<", b")"],
    )
    require(exit_code == 0, f"visual-sentence-unshift vi exited with status {exit_code}")
    require(saved == "One. Two. Three.\n",
            f"unexpected visual-sentence-unshift buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\n\ntwo\n\nthree\n",
        [b">", b"}"],
    )
    require(exit_code == 0, f"visual-paragraph-shift vi exited with status {exit_code}")
    require(saved == "\tone\n\ntwo\n\nthree\n",
            f"unexpected visual-paragraph-shift buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "\tone\n\n\ttwo\n\nthree\n",
        [b"<", b"}"],
    )
    require(exit_code == 0, f"visual-paragraph-unshift vi exited with status {exit_code}")
    require(saved == "one\n\n\ttwo\n\nthree\n",
            f"unexpected visual-paragraph-unshift buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one two three four\nalpha beta gamma\n{ block }\nmark here\n",
        [b"c", b")", b"X", b"\x1b"],
    )
    require(exit_code == 0, f"visual-sentence-change-eof vi exited with status {exit_code}")
    require(saved == "X\n",
            f"unexpected visual-sentence-change-eof buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one two three four\nalpha beta gamma\n{ block }\nmark here\n",
        [b"c", b"}", b"X", b"\x1b"],
    )
    require(exit_code == 0, f"visual-paragraph-change-eof vi exited with status {exit_code}")
    require(saved == "X\n",
            f"unexpected visual-paragraph-change-eof buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one. two. three.\nalpha. beta. gamma.\n",
        [b"$", b"c", b"(", b"X", b"\x1b", b"j", b"."],
    )
    require(exit_code == 0, f"visual-dot-after-cparen vi exited with status {exit_code}")
    require(saved == "one. two. X.\nalpha. Xa. gamma.\n",
            f"unexpected visual-dot-after-cparen buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one. two. three.\nalpha. beta. gamma.\n",
        [b"$", b"c", b")", b"X", b"\x1b", b"j", b"."],
    )
    require(exit_code == 0, f"visual-dot-after-cparen-forward vi exited with status {exit_code}")
    require(saved == "one. two. threeX\nalpha. beta. gaX\n",
            f"unexpected visual-dot-after-cparen-forward buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "alpha beta gamma\nalpha beta gamma\n",
        [b"$", b"c", b"F", b"b", b"X", b"\x1b", b"j", b"."],
    )
    require(exit_code == 0, f"visual-dot-after-cF vi exited with status {exit_code}")
    require(saved == "alpha Xa\nalpha beta gamma\n",
            f"unexpected visual-dot-after-cF buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\n\ntwo\n\nthree\n",
        [b"G", b"c", b"{", b"X", b"\x1b", b"j", b"."],
    )
    require(exit_code == 0, f"visual-dot-after-cbrace vi exited with status {exit_code}")
    require(saved == "one\nX\nthree\n",
            f"unexpected visual-dot-after-cbrace buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\n\ntwo\n\nthree\n",
        [b"c", b"}", b"X", b"\x1b", b"j", b"."],
    )
    require(exit_code == 0, f"visual-dot-after-cbrace-forward vi exited with status {exit_code}")
    require(saved == "X\nX\n\nthree\n",
            f"unexpected visual-dot-after-cbrace-forward buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "".join(f"line {i}\n" for i in range(1, 8)),
        [b"2", b"G", b"c", b"L", b"X", b"\x1b", b"j", b"."],
        rows=6,
        cols=20,
    )
    require(exit_code == 0, f"visual-dot-after-cL vi exited with status {exit_code}")
    require(saved == "line 1\nX\nX\n",
            f"unexpected visual-dot-after-cL buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\ntwo\nthree\nfour\nfive\n",
        [b"3", b"G", b"c", b"H", b"X", b"\x1b", b"j", b"."],
    )
    require(exit_code == 0, f"visual-dot-after-cH vi exited with status {exit_code}")
    require(saved == "X\nfive\n",
            f"unexpected visual-dot-after-cH buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one target\nline two\ntarget three\n",
        [b"c", b"/", b"t", b"a", b"r", b"g", b"e", b"t", b"\r", b"X", b"\x1b",
         b"j", b"."],
    )
    require(exit_code == 0, f"visual-dot-after-cslash vi exited with status {exit_code}")
    require(saved == "Xtarget\nX\ntarget three\n",
            f"unexpected visual-dot-after-cslash buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one target\nline two\ntarget three\n",
        [b"G", b"c", b"?", b"t", b"a", b"r", b"g", b"e", b"t", b"\r", b"X", b"\x1b",
         b"k", b"."],
    )
    require(exit_code == 0, f"visual-dot-after-cquestion vi exited with status {exit_code}")
    require(saved == "one X\ntarget three\n",
            f"unexpected visual-dot-after-cquestion buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one target\nline two\ntarget three\n",
        [b"/", b"t", b"a", b"r", b"g", b"e", b"t", b"\r", b">", b"n"],
    )
    require(exit_code == 0, f"visual-repeat-search-shift vi exited with status {exit_code}")
    require(saved == "\tone target\n\tline two\ntarget three\n",
            f"unexpected visual-repeat-search-shift buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "\tone target\n\tline two\ntarget three\n",
        [b"/", b"t", b"a", b"r", b"g", b"e", b"t", b"\r", b"<", b"N"],
    )
    require(exit_code == 0, f"visual-repeat-search-unshift vi exited with status {exit_code}")
    require(saved == "one target\nline two\ntarget three\n",
            f"unexpected visual-repeat-search-unshift buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one target\nline two\ntarget three\n",
        [b"0", b"/", b"t", b"a", b"r", b"g", b"e", b"t", b"\r", b">", b"*"],
    )
    require(exit_code == 0, f"visual-word-search-shift vi exited with status {exit_code}")
    require(saved == "\tone target\n\tline two\ntarget three\n",
            f"unexpected visual-word-search-shift buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "\tone target\n\tline two\ntarget three\n",
        [b"G", b"0", b"<", b"#"],
    )
    require(exit_code == 0, f"visual-word-search-unshift vi exited with status {exit_code}")
    require(saved == "one target\nline two\ntarget three\n",
            f"unexpected visual-word-search-unshift buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one target\nline two\ntarget three\nline four\ntarget five\n",
        [b"G", b"?", b"t", b"a", b"r", b"g", b"e", b"t", b"\r", b"d", b"2", b"N"],
    )
    require(exit_code == 0, f"visual-counted-reverse-search-delete vi exited with status {exit_code}")
    require(saved == "one \ntarget three\nline four\ntarget five\n",
            f"unexpected visual-counted-reverse-search-delete buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one target\nline two\ntarget three\nline four\ntarget five\n",
        [b"G", b"?", b"t", b"a", b"r", b"g", b"e", b"t", b"\r",
         b"c", b"2", b"N", b"X", b"\x1b"],
    )
    require(exit_code == 0, f"visual-counted-reverse-search-change vi exited with status {exit_code}")
    require(saved == "one X\ntarget three\nline four\ntarget five\n",
            f"unexpected visual-counted-reverse-search-change buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one target\nline two\ntarget three\nline four\ntarget five\n",
        [b"0", b"d", b"2", b"*"],
    )
    require(exit_code == 0, f"visual-counted-word-search-delete vi exited with status {exit_code}")
    require(saved == "one target\nline two\ntarget three\nline four\ntarget five\n",
            f"unexpected visual-counted-word-search-delete buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one target\nline two\ntarget three\nline four\ntarget five\n",
        [b"0", b"c", b"2", b"*", b"X", b"\x1b"],
    )
    require(exit_code == 0, f"visual-counted-word-search-change vi exited with status {exit_code}")
    require(saved == "Xone target\nline two\ntarget three\nline four\ntarget five\n",
            f"unexpected visual-counted-word-search-change buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one target\nline two\ntarget three\nline four\ntarget five\n",
        [b"G", b"0", b"y", b"2", b"#", b"P"],
    )
    require(exit_code == 0, f"visual-counted-word-search-yank vi exited with status {exit_code}")
    require(saved == "one target\nline two\ntarget three\nline fourtarget\nline two\ntarget three\nline four\ntarget five\n",
            f"unexpected visual-counted-word-search-yank buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\ntwo\n",
        [b"i", b"X", b"\x1b", b"j", b"."],
    )
    require(exit_code == 0, f"visual-dot-after-i vi exited with status {exit_code}")
    require(saved == "Xone\nXtwo\n",
            f"unexpected visual-dot-after-i buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\ntwo\n",
        [b"a", b"X", b"\x1b", b"j", b"."],
    )
    require(exit_code == 0, f"visual-dot-after-a vi exited with status {exit_code}")
    require(saved == "oXne\ntwXo\n",
            f"unexpected visual-dot-after-a buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "  one\n  two\n",
        [b"I", b"X", b"\x1b", b"j", b"."],
    )
    require(exit_code == 0, f"visual-dot-after-I vi exited with status {exit_code}")
    require(saved == "  Xone\n  Xtwo\n",
            f"unexpected visual-dot-after-I buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\ntwo\n",
        [b"A", b"X", b"\x1b", b"j", b"."],
    )
    require(exit_code == 0, f"visual-dot-after-A vi exited with status {exit_code}")
    require(saved == "oneX\ntwoX\n",
            f"unexpected visual-dot-after-A buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\ntwo\n",
        [b"o", b"X", b"\x1b", b"k", b"j", b"."],
    )
    require(exit_code == 0, f"visual-dot-after-o vi exited with status {exit_code}")
    require(saved == "one\nX\nX\ntwo\n",
            f"unexpected visual-dot-after-o buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\ntwo\n",
        [b"O", b"X", b"\x1b", b"j", b"."],
    )
    require(exit_code == 0, f"visual-dot-after-O vi exited with status {exit_code}")
    require(saved == "X\nX\none\ntwo\n",
            f"unexpected visual-dot-after-O buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "abc\ndef\n",
        [b"R", b"X", b"Y", b"\x1b", b"j", b"."],
    )
    require(exit_code == 0, f"visual-dot-after-R vi exited with status {exit_code}")
    require(saved == "XYc\ndXY\n",
            f"unexpected visual-dot-after-R buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "alpha beta\ngamma beta\n",
        [b"c", b"w", b"X", b"\x1b", b"j", b"."],
    )
    require(exit_code == 0, f"visual-dot-after-cw vi exited with status {exit_code}")
    require(saved == "X beta\nX beta\n",
            f"unexpected visual-dot-after-cw buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "alpha,beta\ngamma,beta\n",
        [b"c", b"W", b"X", b"\x1b", b"j", b"."],
    )
    require(exit_code == 0, f"visual-dot-after-cW vi exited with status {exit_code}")
    require(saved == "X\nX\n",
            f"unexpected visual-dot-after-cW buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "ab\ncd\n",
        [b"s", b"X", b"\x1b", b"j", b"."],
    )
    require(exit_code == 0, f"visual-dot-after-s vi exited with status {exit_code}")
    require(saved == "Xb\nXd\n",
            f"unexpected visual-dot-after-s buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "alpha beta\ngamma beta\n",
        [b"C", b"X", b"\x1b", b"j", b"."],
    )
    require(exit_code == 0, f"visual-dot-after-C vi exited with status {exit_code}")
    require(saved == "X\nX\n",
            f"unexpected visual-dot-after-C buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "alpha\nbeta\n",
        [b"S", b"X", b"\x1b", b"j", b"."],
    )
    require(exit_code == 0, f"visual-dot-after-S vi exited with status {exit_code}")
    require(saved == "X\nX\n",
            f"unexpected visual-dot-after-S buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\ntwo\n",
        [b"c", b"c", b"X", b"\x1b", b"j", b"."],
    )
    require(exit_code == 0, f"visual-dot-after-cc vi exited with status {exit_code}")
    require(saved == "X\nX\n",
            f"unexpected visual-dot-after-cc buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "ab\ncd\n",
        [b"c", b"l", b"X", b"\x1b", b"j", b"."],
    )
    require(exit_code == 0, f"visual-dot-after-cl vi exited with status {exit_code}")
    require(saved == "Xb\nXd\n",
            f"unexpected visual-dot-after-cl buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "ab\ncd\n",
        [b"l", b"c", b"h", b"X", b"\x1b", b"j", b"."],
    )
    require(exit_code == 0, f"visual-dot-after-ch vi exited with status {exit_code}")
    require(saved == "Xb\nXcd\n",
            f"unexpected visual-dot-after-ch buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "alpha beta\ngamma beta\n",
        [b"c", b"$", b"X", b"\x1b", b"j", b"."],
    )
    require(exit_code == 0, f"visual-dot-after-cdollar vi exited with status {exit_code}")
    require(saved == "X\nX\n",
            f"unexpected visual-dot-after-cdollar buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "alpha beta\ngamma beta\n",
        [b"c", b"e", b"X", b"\x1b", b"j", b"."],
    )
    require(exit_code == 0, f"visual-dot-after-ce vi exited with status {exit_code}")
    require(saved == "X beta\nX beta\n",
            f"unexpected visual-dot-after-ce buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "alpha,beta\ngamma,beta\n",
        [b"c", b"E", b"X", b"\x1b", b"j", b"."],
    )
    require(exit_code == 0, f"visual-dot-after-cE vi exited with status {exit_code}")
    require(saved == "X\nX\n",
            f"unexpected visual-dot-after-cE buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "alpha beta gamma\nalpha beta gamma\n",
        [b"0", b"c", b"f", b"g", b"X", b"\x1b", b"j", b"."],
    )
    require(exit_code == 0, f"visual-dot-after-cf vi exited with status {exit_code}")
    require(saved == "Xamma\nXamma\n",
            f"unexpected visual-dot-after-cf buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "alpha beta gamma\nalpha beta gamma\n",
        [b"0", b"c", b"t", b"g", b"X", b"\x1b", b"j", b"."],
    )
    require(exit_code == 0, f"visual-dot-after-ct vi exited with status {exit_code}")
    require(saved == "Xgamma\nXgamma\n",
            f"unexpected visual-dot-after-ct buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "ab cd\nef gh\n",
        [b"l", b"l", b"c", b"0", b"X", b"\x1b", b"j", b"."],
    )
    require(exit_code == 0, f"visual-dot-after-c0 vi exited with status {exit_code}")
    require(saved == "X cd\nXef gh\n",
            f"unexpected visual-dot-after-c0 buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "  ab\n  cd\n",
        [b"l", b"l", b"c", b"^", b"X", b"\x1b", b"j", b"."],
    )
    require(exit_code == 0, f"visual-dot-after-c^ vi exited with status {exit_code}")
    require(saved == "  Xb\n  Xcd\n",
            f"unexpected visual-dot-after-c^ buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one target\ntwo target\nthree target\n",
        [b"/", b"target", b"\r", b"c", b"n", b"X", b"\x1b", b"n", b"."],
    )
    require(exit_code == 0, f"visual-dot-after-cn vi exited with status {exit_code}")
    require(saved == "one XXtarget\n",
            f"unexpected visual-dot-after-cn buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one target\ntwo target\nthree target\n",
        [b"G", b"?", b"target", b"\r", b"c", b"N", b"X", b"\x1b", b"N", b"."],
    )
    require(exit_code == 0, f"visual-dot-after-cN vi exited with status {exit_code}")
    require(saved == "one Xtarget\n",
            f"unexpected visual-dot-after-cN buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one target\ntwo target\nthree target\n",
        [b"w", b"*", b"c", b"*", b"X", b"\x1b", b"n", b"."],
    )
    require(exit_code == 0, f"visual-dot-after-cstar vi exited with status {exit_code}")
    require(saved == "one Xtarget\ntwo Xtarget\n",
            f"unexpected visual-dot-after-cstar buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one target\ntwo target\nthree target\n",
        [b"G", b"w", b"#", b"c", b"#", b"X", b"\x1b", b"N", b"."],
    )
    require(exit_code == 0, f"visual-dot-after-chash vi exited with status {exit_code}")
    require(saved == "one Xtarget\nthree Xtarget\n",
            f"unexpected visual-dot-after-chash buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "if (x) {\n  foo();\n}\nif (y) {\n  bar();\n}\n",
        [b"f", b"{", b"c", b"%", b"X", b"\x1b", b"j", b"j", b"j", b"f", b"{", b"."],
    )
    require(exit_code == 0, f"visual-dot-after-cpercent vi exited with status {exit_code}")
    require(saved == "if (x) X\nif (y) X\n",
            f"unexpected visual-dot-after-cpercent buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\ntwo\nthree\nfour\n",
        [b"j", b"m", b"a", b"j", b"c", b"'", b"a", b"X", b"\x1b", b"j", b"."],
    )
    require(exit_code == 0, f"visual-dot-after-cmark-line vi exited with status {exit_code}")
    require(saved == "one\nX\n",
            f"unexpected visual-dot-after-cmark-line buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "ab\ncd\nef\n",
        [b"l", b"m", b"a", b"j", b"l", b"c", b"`", b"a", b"X", b"\x1b", b"j", b"."],
    )
    require(exit_code == 0, f"visual-dot-after-cmark-exact vi exited with status {exit_code}")
    require(saved == "aXf\n",
            f"unexpected visual-dot-after-cmark-exact buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\n{\ntwo\n}\nthree\n{\nfour\n}\n",
        [b"d", b"]", b"]"],
    )
    require(exit_code == 0, f"visual-section-delete-forward vi exited with status {exit_code}")
    require(saved == "{\ntwo\n}\nthree\n{\nfour\n}\n",
            f"unexpected visual-section-delete-forward buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\n{\ntwo\n}\nthree\n{\nfour\n}\n",
        [b"y", b"]", b"]", b"P"],
    )
    require(exit_code == 0, f"visual-section-yank-forward vi exited with status {exit_code}")
    require(saved == "one\none\n{\ntwo\n}\nthree\n{\nfour\n}\n",
            f"unexpected visual-section-yank-forward buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\ntwo\n{\nthree\n}\n",
        [b"c", b"]", b"]", b"X", b"\x1b"],
    )
    require(exit_code == 0, f"visual-section-change-forward vi exited with status {exit_code}")
    require("-- INSERT --" in decoded, "missing visual-section-change insert status")
    require(saved == "X\n{\nthree\n}\n",
            f"unexpected visual-section-change-forward buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\n{\ntwo\n}\nthree\n{\nfour\n}\n",
        [b"G", b"d", b"[", b"["],
    )
    require(exit_code == 0, f"visual-section-delete-backward vi exited with status {exit_code}")
    require(saved == "one\n{\ntwo\n}\nthree\n}\n",
            f"unexpected visual-section-delete-backward buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "ab(cd\nef)gh\n",
        [b"g", b"g", b"l", b"l", b"m", b"a", b"j", b"`", b"a", b"r", b"Z"],
    )
    require(exit_code == 0, f"visual-backtick-jump vi exited with status {exit_code}")
    require(saved == "abZcd\nef)gh\n",
            f"unexpected visual-backtick-jump buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "abcdef\n",
        [b"l", b"l", b"m", b"a", b"$", b"d", b"`", b"a"],
    )
    require(exit_code == 0, f"visual-backtick-delete vi exited with status {exit_code}")
    require(saved == "abf\n",
            f"unexpected visual-backtick-delete buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "ab(cd\nef)gh\n",
        [b"g", b"g", b"l", b"l", b"m", b"a", b"G", b"c", b"`", b"a", b"X", b"\x1b"],
    )
    require(exit_code == 0, f"visual-backtick-change-cross vi exited with status {exit_code}")
    require("-- INSERT --" in decoded, "missing visual-backtick-change insert status")
    require(saved == "abX\nef)gh\n",
            f"unexpected visual-backtick-change-cross buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "ab(cd\nef)gh\n",
        [b"g", b"g", b"l", b"l", b"m", b"a", b"G", b"y", b"`", b"a", b"P"],
    )
    require(exit_code == 0, f"visual-backtick-yank-cross vi exited with status {exit_code}")
    require(saved == "ab(cd(cd\nef)gh\n",
            f"unexpected visual-backtick-yank-cross buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "aa bb\ncc\n",
        [b"0", b"l", b"m", b"a", b"0", b">", b"`", b"a"],
    )
    require(exit_code == 0, f"visual-backtick-shift-same-line vi exited with status {exit_code}")
    require(saved == "\taa bb\ncc\n",
            f"unexpected visual-backtick-shift-same-line buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "aa\nbb\ncc\ndd\n",
        [b"g", b"g", b"0", b"m", b"a", b"G", b"$", b">", b"`", b"a"],
    )
    require(exit_code == 0, f"visual-backtick-shift-cross vi exited with status {exit_code}")
    require(saved == "\taa\n\tbb\n\tcc\n\tdd\n",
            f"unexpected visual-backtick-shift-cross buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "aa\nbb\ncc\ndd\n",
        [b"g", b"g", b"0", b"m", b"a", b"G", b"$", b"<", b"`", b"a"],
    )
    require(exit_code == 0, f"visual-backtick-unshift-cross vi exited with status {exit_code}")
    require(saved == "aa\nbb\ncc\ndd\n",
            f"unexpected visual-backtick-unshift-cross buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "aa\nbb\ncc\ndd\n",
        [b"g", b"g", b"0", b">", b"*"],
    )
    require(exit_code == 0, f"visual-star-shift-wrap vi exited with status {exit_code}")
    require(saved == "\taa\nbb\ncc\ndd\n",
            f"unexpected visual-star-shift-wrap buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "aa\nbb\ncc\ndd\n",
        [b"g", b"g", b"0", b">", b"*", b">", b"n"],
    )
    require(exit_code == 0, f"visual-star-shift-repeat vi exited with status {exit_code}")
    require(saved == "\t\taa\nbb\ncc\ndd\n",
            f"unexpected visual-star-shift-repeat buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "aa\nbb\ncc\ndd\n",
        [b"G", b"0", b">", b"#", b">", b"n"],
    )
    require(exit_code == 0, f"visual-hash-shift-repeat vi exited with status {exit_code}")
    require(saved == "aa\nbb\ncc\n\t\tdd\n",
            f"unexpected visual-hash-shift-repeat buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "aa\nbb\ncc\ndd\n",
        [b"G", b"0", b"F", b"d", b">", b","],
    )
    require(exit_code == 0, f"visual-find-comma-shift vi exited with status {exit_code}")
    require(saved == "aa\nbb\ncc\n\tdd\n",
            f"unexpected visual-find-comma-shift buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "ab(cd\nef)gh\n",
        [b"0", b"f", b"(", b"d", b"%"],
    )
    require(exit_code == 0, f"visual-percent-delete-cross vi exited with status {exit_code}")
    require(saved == "abgh\n",
            f"unexpected visual-percent-delete-cross buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "ab(cd\nef)gh\n",
        [b"0", b"f", b"(", b"c", b"%", b"X", b"\x1b"],
    )
    require(exit_code == 0, f"visual-percent-change-cross vi exited with status {exit_code}")
    require("-- INSERT --" in decoded, "missing visual-percent-change insert status")
    require(saved == "abXgh\n",
            f"unexpected visual-percent-change-cross buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "ab(cd\nef)gh\n",
        [b"0", b"f", b"(", b"y", b"%", b"P"],
    )
    require(exit_code == 0, f"visual-percent-yank-cross vi exited with status {exit_code}")
    require(saved == "ab(cd\nef)(cd\nef)gh\n",
            f"unexpected visual-percent-yank-cross buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "ab(cd\nef)gh\n",
        [b"G", b"f", b")", b"d", b"%"],
    )
    require(exit_code == 0, f"visual-percent-delete-cross-backward vi exited with status {exit_code}")
    require(saved == "abgh\n",
            f"unexpected visual-percent-delete-cross-backward buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "ab(cd\nef)gh\n",
        [b"G", b"f", b")", b"c", b"%", b"X", b"\x1b"],
    )
    require(exit_code == 0, f"visual-percent-change-cross-backward vi exited with status {exit_code}")
    require("-- INSERT --" in decoded,
            "missing visual-percent-change-cross-backward insert status")
    require(saved == "abXgh\n",
            f"unexpected visual-percent-change-cross-backward buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "ab(cd\nef)gh\n",
        [b"G", b"f", b")", b"y", b"%", b"P"],
    )
    require(exit_code == 0, f"visual-percent-yank-cross-backward vi exited with status {exit_code}")
    require(saved == "ab(cd\nef)(cd\nef)gh\n",
            f"unexpected visual-percent-yank-cross-backward buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "a (b [c] d) e\nnext line\n",
        [b"0", b"%", b"r", b"X"],
    )
    require(exit_code == 0, f"visual-percent-scan vi exited with status {exit_code}")
    require(saved == "a (b [c] dX e\nnext line\n",
            f"unexpected visual-percent-scan buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "a (b [c] d) e\nnext line\n",
        [b"0", b"d", b"%"],
    )
    require(exit_code == 0, f"visual-percent-scan-delete vi exited with status {exit_code}")
    require(saved == " e\nnext line\n",
            f"unexpected visual-percent-scan-delete buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "a (b [c] d) e\nnext line\n",
        [b"0", b"c", b"%", b"X", b"\x1b"],
    )
    require(exit_code == 0, f"visual-percent-scan-change vi exited with status {exit_code}")
    require("-- INSERT --" in decoded, "missing visual-percent-scan change insert status")
    require(saved == "X e\nnext line\n",
            f"unexpected visual-percent-scan-change buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "a (b [c] d) e\nnext line\n",
        [b"0", b"y", b"%", b"P"],
    )
    require(exit_code == 0, f"visual-percent-scan-yank vi exited with status {exit_code}")
    require(saved == "a (b [c] d)a (b [c] d) e\nnext line\n",
            f"unexpected visual-percent-scan-yank buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "if (x) {\nalpha\n}\n",
        [b"0", b"%", b"r", b"Z"],
    )
    require(exit_code == 0, f"visual-percent-scan-cross vi exited with status {exit_code}")
    require(saved == "if (x) Z\nalpha\n}\n",
            f"unexpected visual-percent-scan-cross buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "if (x) {\nalpha\n}\n",
        [b"0", b"d", b"%"],
    )
    require(exit_code == 0, f"visual-percent-scan-cross-delete vi exited with status {exit_code}")
    require(saved == "if (x) \n",
            f"unexpected visual-percent-scan-cross-delete buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "if (x) {\nalpha\n}\n",
        [b"0", b"c", b"%", b"X", b"\x1b"],
    )
    require(exit_code == 0, f"visual-percent-scan-cross-change vi exited with status {exit_code}")
    require("-- INSERT --" in decoded, "missing visual-percent-scan-cross change insert status")
    require(saved == "if (x) X\n",
            f"unexpected visual-percent-scan-cross-change buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "if (x) {\nalpha\n}\n",
        [b"0", b"y", b"%", b"P"],
    )
    require(exit_code == 0, f"visual-percent-scan-cross-yank vi exited with status {exit_code}")
    require(saved == "if (x) {\nalpha\n}{\nalpha\n}\n",
            f"unexpected visual-percent-scan-cross-yank buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "if (x) {\n    one\n    two\n}\n",
        [b"0", b">", b"%"],
    )
    require(exit_code == 0, f"visual-percent-shift-scan vi exited with status {exit_code}")
    require(saved == "\tif (x) {\n\t    one\n\t    two\n\t}\n",
            f"unexpected visual-percent-shift-scan buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "if (x) {\n    one\n    two\n}\n",
        [b"0", b"f", b"{", b">", b"%"],
    )
    require(exit_code == 0, f"visual-percent-shift-cross vi exited with status {exit_code}")
    require(saved == "\tif (x) {\n\t    one\n\t    two\n\t}\n",
            f"unexpected visual-percent-shift-cross buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "if (x) {\n    one\n    two\n}\n",
        [b"5", b"0", b">", b"%"],
    )
    require(exit_code == 0, f"visual-percent-shift-count vi exited with status {exit_code}")
    require(saved == "\tif (x) {\n\t    one\n    two\n}\n",
            f"unexpected visual-percent-shift-count buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "if (x) {\n    one\n    two\n}\n",
        [b"0", b"f", b"{", b"<", b"%"],
    )
    require(exit_code == 0, f"visual-percent-unshift-cross vi exited with status {exit_code}")
    require(saved == "if (x) {\none\ntwo\n}\n",
            f"unexpected visual-percent-unshift-cross buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "abc def\n",
        [b"0", b"f", b"d", b"m", b"a", b"0", b"d", b"`", b"a"],
    )
    require(exit_code == 0, f"visual-exact-mark-delete vi exited with status {exit_code}")
    require(saved == "def\n",
            f"unexpected visual-exact-mark-delete buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "abc def\n",
        [b"0", b"f", b"d", b"m", b"a", b"0", b"c", b"`", b"a", b"X", b"\x1b"],
    )
    require(exit_code == 0, f"visual-exact-mark-change vi exited with status {exit_code}")
    require("-- INSERT --" in decoded, "missing visual-exact-mark change insert status")
    require(saved == "Xdef\n",
            f"unexpected visual-exact-mark-change buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "abc def\n",
        [b"0", b"f", b"d", b"m", b"a", b"0", b"y", b"`", b"a", b"P"],
    )
    require(exit_code == 0, f"visual-exact-mark-yank vi exited with status {exit_code}")
    require(saved == "abc abc def\n",
            f"unexpected visual-exact-mark-yank buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "abc def\n",
        [b"0", b"f", b"d", b"m", b"a", b"0", b">", b"`", b"a"],
    )
    require(exit_code == 0, f"visual-exact-mark-shift vi exited with status {exit_code}")
    require(saved == "\tabc def\n",
            f"unexpected visual-exact-mark-shift buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "\tabc def\n",
        [b"0", b"f", b"d", b"m", b"a", b"0", b"<", b"`", b"a"],
    )
    require(exit_code == 0, f"visual-exact-mark-unshift vi exited with status {exit_code}")
    require(saved == "abc def\n",
            f"unexpected visual-exact-mark-unshift buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\ntwo\nthree\n",
        [b"j", b"l", b"m", b"a", b"G", b"d", b"`", b"a"],
    )
    require(exit_code == 0, f"visual-exact-mark-delete-cross-backward vi exited with status {exit_code}")
    require(saved == "one\nt\nthree\n",
            f"unexpected visual-exact-mark-delete-cross-backward buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\ntwo\nthree\n",
        [b"G", b"l", b"m", b"a", b"g", b"g", b"d", b"`", b"a"],
    )
    require(exit_code == 0, f"visual-exact-mark-delete-cross-forward vi exited with status {exit_code}")
    require(saved == "hree\n",
            f"unexpected visual-exact-mark-delete-cross-forward buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\ntwo\nthree\n",
        [b"G", b"l", b"m", b"a", b"g", b"g", b"c", b"`", b"a", b"X", b"\x1b"],
    )
    require(exit_code == 0, f"visual-exact-mark-change-cross-forward vi exited with status {exit_code}")
    require("-- INSERT --" in decoded,
            "missing visual-exact-mark-change-cross-forward insert status")
    require(saved == "Xhree\n",
            f"unexpected visual-exact-mark-change-cross-forward buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\ntwo\nthree\n",
        [b"G", b"l", b"m", b"a", b"g", b"g", b"y", b"`", b"a", b"P"],
    )
    require(exit_code == 0, f"visual-exact-mark-yank-cross-forward vi exited with status {exit_code}")
    require(saved == "one\ntwo\ntone\ntwo\nthree\n",
            f"unexpected visual-exact-mark-yank-cross-forward buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "  one\n  two\n  three\n",
        [b"j", b"j", b"y", b"-", b"P"],
    )
    require(exit_code == 0, f"yminus-indent vi exited with status {exit_code}")
    require(saved == "  one\n  two\n  three\n  two\n  three\n",
            f"unexpected yminus-indent buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "l1\nl2\nl3\nl4\nl5\nl6\n",
        [b"4", b"G", b"y", b"H", b"P"],
        rows=6,
        cols=20,
    )
    require(exit_code == 0, f"yH-screen vi exited with status {exit_code}")
    require(saved == "l1\nl2\nl3\nl4\nl1\nl2\nl3\nl4\nl5\nl6\n",
            f"unexpected yH-screen buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "l1\nl2\nl3\nl4\nl5\nl6\n",
        [b"5", b"G", b"y", b"M", b"P"],
        rows=6,
        cols=20,
    )
    require(exit_code == 0, f"yM-screen vi exited with status {exit_code}")
    require(saved == "l1\nl2\nl3\nl4\nl5\nl3\nl4\nl5\nl6\n",
            f"unexpected yM-screen buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one two\nthree four\n",
        [b">", b"$"],
    )
    require(exit_code == 0, f"shift-dollar-eol vi exited with status {exit_code}")
    require(saved == "\tone two\nthree four\n",
            f"unexpected shift-dollar-eol buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "\tone two\n\tthree four\n",
        [b"<", b"$"],
    )
    require(exit_code == 0, f"unshift-dollar-eol vi exited with status {exit_code}")
    require(saved == "one two\n\tthree four\n",
            f"unexpected unshift-dollar-eol buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one.\n\ntwo.\nthree.\n\nfour.\n",
        [b"j", b">", b")"],
    )
    require(exit_code == 0, f"blank-sep-shift-rparen vi exited with status {exit_code}")
    require(saved == "one.\n\ntwo.\nthree.\n\nfour.\n",
            f"unexpected blank-sep-shift-rparen buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "\tone.\n\n\ttwo.\n\tthree.\n\n\tfour.\n",
        [b"j", b">", b")"],
    )
    require(exit_code == 0, f"blank-sep-shift-rparen-indented vi exited with status {exit_code}")
    require(saved == "\tone.\n\n\t\ttwo.\n\tthree.\n\n\tfour.\n",
            f"unexpected blank-sep-shift-rparen-indented buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "\tone.\n\n\ttwo.\n\tthree.\n\n\tfour.\n",
        [b"j", b"<", b")"],
    )
    require(exit_code == 0, f"blank-sep-unshift-rparen-indented vi exited with status {exit_code}")
    require(saved == "\tone.\n\ntwo.\n\tthree.\n\n\tfour.\n",
            f"unexpected blank-sep-unshift-rparen-indented buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\n\ntwo\nthree\n\nfour\n",
        [b"j", b"d", b"}"],
    )
    require(exit_code == 0, f"blank-sep-delete-rbrace vi exited with status {exit_code}")
    require(saved == "one\n\nfour\n",
            f"unexpected blank-sep-delete-rbrace buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\n\ntwo\nthree\n\nfour\n",
        [b"j", b"c", b"}", b"X", b"\x1b"],
    )
    require(exit_code == 0, f"blank-sep-change-rbrace vi exited with status {exit_code}")
    require(saved == "one\nX\n\nfour\n",
            f"unexpected blank-sep-change-rbrace buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\n\ntwo\nthree\n\nfour\n",
        [b"j", b"y", b"}", b"P"],
    )
    require(exit_code == 0, f"blank-sep-yank-rbrace vi exited with status {exit_code}")
    require(saved == "one\n\ntwo\nthree\n\ntwo\nthree\n\nfour\n",
            f"unexpected blank-sep-yank-rbrace buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "  one\n  two\n",
        [b"c", b"^", b"X", b"\x1b"],
    )
    require(exit_code == 0, f"change-caret-indent vi exited with status {exit_code}")
    require(saved == "  Xone\n  two\n",
            f"unexpected change-caret-indent buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "  one\n  two\n",
        [b">", b"^"],
    )
    require(exit_code == 0, f"shift-caret-indent vi exited with status {exit_code}")
    require(saved == "\t  one\n  two\n",
            f"unexpected shift-caret-indent buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "\t  one\n\t  two\n",
        [b"<", b"^"],
    )
    require(exit_code == 0, f"unshift-caret-indent vi exited with status {exit_code}")
    require(saved == "  one\n\t  two\n",
            f"unexpected unshift-caret-indent buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "  one\n  two\n",
        [b">", b"0"],
    )
    require(exit_code == 0, f"shift-zero vi exited with status {exit_code}")
    require(saved == "\t  one\n  two\n",
            f"unexpected shift-zero buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "\t  one\n\t  two\n",
        [b"<", b"0"],
    )
    require(exit_code == 0, f"unshift-zero vi exited with status {exit_code}")
    require(saved == "  one\n\t  two\n",
            f"unexpected unshift-zero buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "  one\n  two\n",
        [b">", b"1", b"|"],
    )
    require(exit_code == 0, f"shift-pipe vi exited with status {exit_code}")
    require(saved == "\t  one\n  two\n",
            f"unexpected shift-pipe buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "\t  one\n\t  two\n",
        [b"<", b"1", b"|"],
    )
    require(exit_code == 0, f"unshift-pipe vi exited with status {exit_code}")
    require(saved == "  one\n\t  two\n",
            f"unexpected unshift-pipe buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\ntwo\nthree\n",
        [b"3", b"G", b">", b"g", b"g"],
    )
    require(exit_code == 0, f"shift-gg vi exited with status {exit_code}")
    require(saved == "\tone\n\ttwo\n\tthree\n",
            f"unexpected shift-gg buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "\tone\n\ttwo\n\tthree\n",
        [b"3", b"G", b"<", b"g", b"g"],
    )
    require(exit_code == 0, f"unshift-gg vi exited with status {exit_code}")
    require(saved == "one\ntwo\nthree\n",
            f"unexpected unshift-gg buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\ntwo\nthree\n",
        [b">", b"G"],
    )
    require(exit_code == 0, f"shift-G vi exited with status {exit_code}")
    require(saved == "\tone\n\ttwo\n\tthree\n",
            f"unexpected shift-G buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "\tone\n\ttwo\n\tthree\n",
        [b"<", b"G"],
    )
    require(exit_code == 0, f"unshift-G vi exited with status {exit_code}")
    require(saved == "one\ntwo\nthree\n",
            f"unexpected unshift-G buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\ntwo\nthree\n",
        [b">", b"+"],
    )
    require(exit_code == 0, f"shift-plus-line vi exited with status {exit_code}")
    require(saved == "\tone\n\ttwo\nthree\n",
            f"unexpected shift-plus-line buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "\tone\n\ttwo\n\tthree\n",
        [b"<", b"+"],
    )
    require(exit_code == 0, f"unshift-plus-line vi exited with status {exit_code}")
    require(saved == "one\ntwo\n\tthree\n",
            f"unexpected unshift-plus-line buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\ntwo\nthree\n",
        [b">", b"\r"],
    )
    require(exit_code == 0, f"shift-enter-line vi exited with status {exit_code}")
    require(saved == "\tone\n\ttwo\nthree\n",
            f"unexpected shift-enter-line buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "\tone\n\ttwo\n\tthree\n",
        [b"<", b"\r"],
    )
    require(exit_code == 0, f"unshift-enter-line vi exited with status {exit_code}")
    require(saved == "one\ntwo\n\tthree\n",
            f"unexpected unshift-enter-line buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\ntwo\nthree\n",
        [b">", b"\n"],
    )
    require(exit_code == 0, f"shift-newline-line vi exited with status {exit_code}")
    require(saved == "\tone\n\ttwo\nthree\n",
            f"unexpected shift-newline-line buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "\tone\n\ttwo\n\tthree\n",
        [b"<", b"\n"],
    )
    require(exit_code == 0, f"unshift-newline-line vi exited with status {exit_code}")
    require(saved == "one\ntwo\n\tthree\n",
            f"unexpected unshift-newline-line buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\ntwo\nthree\n",
        [b"d", b"+"],
    )
    require(exit_code == 0, f"d-plus vi exited with status {exit_code}")
    require(saved == "three\n",
            f"unexpected d-plus buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\ntwo\nthree\n",
        [b"c", b"\r", b"X", b"\x1b"],
    )
    require(exit_code == 0, f"c-enter vi exited with status {exit_code}")
    require("-- INSERT --" in decoded, "missing c-enter insert status")
    require(saved == "X\nthree\n",
            f"unexpected c-enter buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\ntwo\nthree\n",
        [b"G", b"y", b"-", b"P"],
    )
    require(exit_code == 0, f"y-minus vi exited with status {exit_code}")
    require(saved == "one\ntwo\nthree\ntwo\nthree\n",
            f"unexpected y-minus buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\ntwo\nthree\n",
        [b"d", b"\r"],
    )
    require(exit_code == 0, f"d-enter vi exited with status {exit_code}")
    require(saved == "three\n",
            f"unexpected d-enter buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\ntwo\nthree\n",
        [b"y", b"\r", b"P"],
    )
    require(exit_code == 0, f"y-enter vi exited with status {exit_code}")
    require(saved == "one\ntwo\none\ntwo\nthree\n",
            f"unexpected y-enter buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\ntwo\nthree\n",
        [b"d", b"\n"],
    )
    require(exit_code == 0, f"d-newline vi exited with status {exit_code}")
    require(saved == "three\n",
            f"unexpected d-newline buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\ntwo\nthree\n",
        [b"c", b"\n", b"X", b"\x1b"],
    )
    require(exit_code == 0, f"c-newline vi exited with status {exit_code}")
    require("-- INSERT --" in decoded, "missing c-newline insert status")
    require(saved == "X\nthree\n",
            f"unexpected c-newline buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\ntwo\nthree\n",
        [b"y", b"\n", b"P"],
    )
    require(exit_code == 0, f"y-newline vi exited with status {exit_code}")
    require(saved == "one\ntwo\none\ntwo\nthree\n",
            f"unexpected y-newline buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\ntwo\nthree\nfour\n",
        [b"j", b"j", b"c", b"-", b"X", b"\x1b"],
    )
    require(exit_code == 0, f"c-minus vi exited with status {exit_code}")
    require("-- INSERT --" in decoded, "missing c-minus insert status")
    require(saved == "one\nX\nfour\n",
            f"unexpected c-minus buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\ntwo\nthree\n",
        [b">", b"_"],
    )
    require(exit_code == 0, f"shift-underscore-line vi exited with status {exit_code}")
    require(saved == "\tone\ntwo\nthree\n",
            f"unexpected shift-underscore-line buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "\tone\n\ttwo\n\tthree\n",
        [b"<", b"_"],
    )
    require(exit_code == 0, f"unshift-underscore-line vi exited with status {exit_code}")
    require(saved == "one\n\ttwo\n\tthree\n",
            f"unexpected unshift-underscore-line buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\ntwo\nthree\nfour\nfive\n",
        [b"4", b"G", b">", b"H"],
        rows=5,
        cols=20,
    )
    require(exit_code == 0, f"shift-H vi exited with status {exit_code}")
    require(saved == "\tone\n\ttwo\n\tthree\n\tfour\nfive\n",
            f"unexpected shift-H buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "\tone\n\ttwo\n\tthree\n\tfour\n\tfive\n",
        [b"4", b"G", b"<", b"H"],
        rows=5,
        cols=20,
    )
    require(exit_code == 0, f"unshift-H vi exited with status {exit_code}")
    require(saved == "one\ntwo\nthree\nfour\n\tfive\n",
            f"unexpected unshift-H buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\ntwo\nthree\nfour\nfive\n",
        [b"2", b"G", b">", b"M"],
        rows=5,
        cols=20,
    )
    require(exit_code == 0, f"shift-M vi exited with status {exit_code}")
    require(saved == "one\n\ttwo\nthree\nfour\nfive\n",
            f"unexpected shift-M buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "\tone\n\ttwo\n\tthree\n\tfour\n\tfive\n",
        [b"2", b"G", b"<", b"M"],
        rows=5,
        cols=20,
    )
    require(exit_code == 0, f"unshift-M vi exited with status {exit_code}")
    require(saved == "\tone\ntwo\n\tthree\n\tfour\n\tfive\n",
            f"unexpected unshift-M buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one\ntwo\nthree\nfour\nfive\n",
        [b">", b"L"],
        rows=5,
        cols=20,
    )
    require(exit_code == 0, f"shift-L vi exited with status {exit_code}")
    require(saved == "\tone\n\ttwo\n\tthree\n\tfour\nfive\n",
            f"unexpected shift-L buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "\tone\n\ttwo\n\tthree\n\tfour\n\tfive\n",
        [b"<", b"L"],
        rows=5,
        cols=20,
    )
    require(exit_code == 0, f"unshift-L vi exited with status {exit_code}")
    require(saved == "one\ntwo\nthree\nfour\n\tfive\n",
            f"unexpected unshift-L buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "\t  one\n\t  two\n\t  three\n",
        [b"<", b"+"],
    )
    require(exit_code == 0, f"unshift-plus-indent vi exited with status {exit_code}")
    require(saved == "  one\n  two\n\t  three\n",
            f"unexpected unshift-plus-indent buffer: {saved!r}")

    scroll_operator_initial = (
        "line 1\nline 2\nline 3\nline 4\n"
        "line 5\nline 6\nline 7\nline 8\n"
    )
    for name, keys in [
        ("d-ctrl-d-noop", [b"4", b"G", b"d", b"\x04"]),
        ("c-ctrl-d-noop", [b"4", b"G", b"c", b"\x04", b"X", b"\x1b"]),
        ("y-ctrl-d-noop", [b"4", b"G", b"y", b"\x04", b"P"]),
        ("shift-ctrl-d-noop", [b"4", b"G", b">", b"\x04"]),
        ("unshift-ctrl-d-noop", [b"4", b"G", b"<", b"\x04"]),
        ("d-ctrl-u-noop", [b"4", b"G", b"d", b"\x15"]),
        ("c-ctrl-u-noop", [b"4", b"G", b"c", b"\x15", b"X", b"\x1b"]),
        ("y-ctrl-u-noop", [b"4", b"G", b"y", b"\x15", b"P"]),
        ("shift-ctrl-u-noop", [b"4", b"G", b">", b"\x15"]),
        ("unshift-ctrl-u-noop", [b"4", b"G", b"<", b"\x15"]),
        ("d-ctrl-e-noop", [b"4", b"G", b"d", b"\x05"]),
        ("c-ctrl-e-noop", [b"4", b"G", b"c", b"\x05", b"X", b"\x1b"]),
        ("y-ctrl-e-noop", [b"4", b"G", b"y", b"\x05", b"P"]),
        ("shift-ctrl-e-noop", [b"4", b"G", b">", b"\x05"]),
        ("unshift-ctrl-e-noop", [b"4", b"G", b"<", b"\x05"]),
        ("d-ctrl-y-noop", [b"4", b"G", b"d", b"\x19"]),
        ("c-ctrl-y-noop", [b"4", b"G", b"c", b"\x19", b"X", b"\x1b"]),
        ("y-ctrl-y-noop", [b"4", b"G", b"y", b"\x19", b"P"]),
        ("shift-ctrl-y-noop", [b"4", b"G", b">", b"\x19"]),
        ("unshift-ctrl-y-noop", [b"4", b"G", b"<", b"\x19"]),
    ]:
        exit_code, decoded, saved = run_vi_session(
            vi_path,
            scroll_operator_initial,
            keys,
            rows=6,
            cols=20,
        )
        require(exit_code == 0, f"{name} vi exited with status {exit_code}")
        require(saved == scroll_operator_initial,
                f"unexpected {name} buffer: {saved!r}")
        if name.startswith("c-"):
            require("-- INSERT --" not in decoded,
                    f"{name} unexpectedly entered insert mode")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one two three four five six\n\nalpha beta gamma delta\n\nsec\n{\nbody\n}\n",
        [b"2", b"]", b"]", b"r", b"X"],
    )
    require(exit_code == 0, f"counted-section-motion vi exited with status {exit_code}")
    require(saved == "one two three four five six\n\nalpha beta gamma delta\n\nsec\n{\nbody\nX\n",
            f"unexpected counted-section-motion buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one two three four five six\n\nalpha beta gamma delta\n\nsec\n{\nbody\n}\n",
        [b"2", b"d", b"]", b"]"],
    )
    require(exit_code == 0, f"counted-section-delete vi exited with status {exit_code}")
    require(saved == "}\n",
            f"unexpected counted-section-delete buffer: {saved!r}")

    exit_code, decoded, saved = run_vi_session(
        vi_path,
        "one two three four five six\n\nalpha beta gamma delta\n\nsec\n{\nbody\n}\n",
        [b"2", b"y", b"]", b"]", b"P"],
    )
    require(exit_code == 0, f"counted-section-yank vi exited with status {exit_code}")
    require(saved == "one two three four five six\n\nalpha beta gamma delta\n\nsec\n{\nbody\none two three four five six\n\nalpha beta gamma delta\n\nsec\n{\nbody\n}\n",
            f"unexpected counted-section-yank buffer: {saved!r}")
    print("vi pty test: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
