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

## Status — WORKING (wired into rc.d as 15-rpcbind)
rpcbind runs correctly on substrate, both as a foreground server (`rpcbind -f`)
and via the daemonized rc.d path (`/etc/rc.d/15-rpcbind`).  It binds the IPv4
portmapper on 127.0.0.1:111 and its local `AF_UNIX` socket
`/var/run/rpcbind.sock`, and CDE's ToolTalk (`ttsession`) registers
successfully — `ttsession -c /bin/true` exits 0, so the full CDE session's
messaging system starts.

Getting here required several substrate fixes, all committed:
- `getsockopt(SO_TYPE)` (libtirpc `svc_tli_create` switches on it),
- `fork()` inheriting the per-thread TLS `gs_base` (rpcbind's `daemon(0,0)`
  child faulted on its first `%gs` access without it),
- `struct in6_addr`'s `s6_addr32` union view,
- libtirpc tolerating a missing `/proc/sys/net/ipv4/ip_local_reserved_ports`,
- `/etc/services` carrying the `sunrpc` (111) entries — without them
  `getaddrinfo("sunrpc")` returned `EAI_SERVICE` and the IPv4 listener never
  bound,
- AF_INET `bind(port 0)` assigning an ephemeral port up front (POSIX), so a
  service's `getsockname()`-derived registration uses its real port instead of
  registering port 0 and then conflicting with its own re-registration.

> An earlier note here claimed rpcbind crashed substrate into a reset loop.
> That was a test-harness artifact: a `debugfs`-corrupted `/sbin/rpcbind`
> binary plus running a shell as init.  Under real init via rc.d with a clean
> binary, rpcbind is stable.
