# rpcbind on Substrate

The Sun RPC portmapper (port 111 + `/var/run/rpcbind.sock`).  CDE's ToolTalk
`ttsession` registers its RPC service with `pmap_set(3)` / `svc_register(3)`,
which require a running portmapper — so without rpcbind, ToolTalk (and thus a
full CDE session) fails with **"The desktop messaging system could not be
started."**

## Layout
- `fetch.sh` — download rpcbind 1.2.6 (SHA-256 verified) + extract.
- `build.sh` — cross-configure/build/stage into `dist-rpcbind/` (`/sbin/rpcbind`).

## Build notes
- Depends on `contrib/libtirpc` (headers + `libtirpc.so.3`).
- rpcbind ships no `config.sub`/`config.guess`; the build borrows the
  (substrate-aware) pair from the libtirpc tree.
- `src/rpcb_svc_com.c` includes glibc-internal `<bits/poll.h>` right after
  `<poll.h>`; substrate has no `bits/poll.h` and `<poll.h>` already defines
  `POLLIN`/… so the build drops it (sed, idempotent).
- `--disable-libwrap`, `--without-systemdsystemunitdir`, `--with-rpcuser=root`,
  `--with-statedir=/var/run`.
- Needs `struct in6_addr`'s `s6_addr32` union view (added to substrate's
  `<netinet/in.h>`).

## Status — KNOWN-BROKEN AT RUNTIME (do NOT wire into rc.d yet)
rpcbind cross-builds cleanly, but it does **not** run correctly on substrate:
started as a child it `exit(0)`s without binding port 111 / creating the
socket, and its daemon/session setup (it detaches the controlling terminal)
appears to SIGHUP the init shell, after which the kernel resets — booting
substrate into a reset loop.  This is a substrate TTY/session-handling
interaction (controlling-terminal revocation against the session/init), not an
rpcbind bug, and needs dedicated kernel debugging before rpcbind can be enabled
at boot.  Until then ToolTalk (and the full CDE session) cannot start; CDE's
"Failsafe Session" (dtwm + xterm, no ToolTalk) is the fallback.
