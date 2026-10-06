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
- The native 64-bit build works as the 32-bit one does.  It used to lose
  panes and sessions about half the time: `CMSG_DATA` in `<sys/socket.h>`
  put a control message's data 12 bytes in, while `CMSG_LEN` counted a
  16-byte header for a 64-bit process.  tmux's imsg code counts received
  descriptors as what lies between `CMSG_DATA` and the end of the message,
  got two for the one the client sent, and closed whatever number was in
  the four bytes after it -- a descriptor of the server's own.  Fixed in
  the header and in the kernel's side of the layout.
