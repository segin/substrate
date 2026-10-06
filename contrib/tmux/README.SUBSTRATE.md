# tmux on substrate

tmux 3.7c, the terminal multiplexer.

- `fetch.sh` downloads the release tarball from GitHub and checks it
  against the digest GitHub records for the asset.
- `build.sh` cross-builds it against `contrib/libevent` and
  `contrib/ncurses` (taken from their staging trees) and stages
  `/usr/bin/tmux` and its manual page under `dist-overlay/dist-tmux/usr/`.

No patches.  Three things on substrate's side had to be put right for it,
which is where they belonged:

- **`WCHAR_MAX` and `WCHAR_MIN`** were missing from `<wchar.h>` and
  `<stdint.h>`; `utf8.c` did not compile.
- **`sendmsg`/`recvmsg` took at most 64 iovecs** where `<limits.h>` says
  `IOV_MAX` is 1024.  tmux hands `sendmsg` every message it has queued, a
  new client queues one per environment variable and terminfo capability,
  and the call came back `EMSGSIZE`: "server exited unexpectedly" on every
  command.
- **Terminal job control was applied to terminals that were not the
  caller's controlling terminal.**  The tmux server reads its client's
  terminal from another session; every such read was answered `EINTR` and
  a `SIGTTIN`, and the server dropped the client the moment it attached.

## What works

On the 32-bit userland, under both kernels: detached and attached
sessions, windows, split panes, `send-keys`, `capture-pane`, detaching and
reattaching, a pane split from inside tmux, `kill-server`.

## Known limitations

- Built for the `substrate` host, so the per-platform file is
  `osdep-unknown.c`: tmux is not told the name or working directory of the
  process in a pane.  Automatic window names stay at the command the
  window was started with, and `#{pane_current_path}` is empty.
- No utf8proc; tmux uses its own width tables.
- **The native 64-bit build is not reliable.**  It builds and starts, and
  a session often works, but panes die shortly after they are created
  often enough that it cannot be depended on.  The 32-bit binary on the
  64-bit kernel does not have the problem, so it is in the native 64-bit
  userland path and not in the kernel changes above; it has not been run
  down.
