# libtirpc 1.3.5 — substrate port

The transport-independent Sun RPC library (the RPC that used to live in
glibc).  CDE's **ToolTalk** — its IPC backbone — needs Sun RPC (`<rpc/rpc.h>`,
XDR, `svc_register`/`clnt_create`/...), which substrate's libc does not
provide.  This is the critical-path dependency for the CDE desktop.

## Build

```sh
./fetch.sh
./build.sh        # -> dist-libtirpc/usr/{lib/libtirpc.so.3, include/tirpc/rpc/*.h}
```

Built `--disable-gssapi` (no Kerberos on substrate) and `--disable-static`.
Headers install under `/usr/include/tirpc`; consumers add `-I.../tirpc`
(libtirpc.pc records it).

## Substrate notes

Standard autotools adjustments (config.sub OS, libtool shared-lib cases) plus:

- **`-D__linux__`** — libtirpc gates its pthread thread-abstraction
  (`reentrant.h`) and reserved-port handling on `__linux__`; substrate is
  pthread + ELF + BSD-sockets, so the Linux path is correct.  Only two such
  guards exist in the tree, both appropriate.
- **`-std=gnu11`** — old `()` prototypes (GCC 16/C23 would treat them as
  `(void)`); **`-include string.h -include stdlib.h`** — several files use
  `memset`/`malloc` without the include; **`-Wno-error=...`** — demote GCC 16's
  promoted-to-error legacy warnings.
- **`LIBS=-lpthread`** — force the pthread link (its autodetect didn't add it
  to the library link).

Bringing it up filled a large batch of missing POSIX/BSD surface in substrate's
libc and headers, fixed in-tree (see the accompanying libc commit): `rand_r`
declaration, `<sys/queue.h>`, the `uint`/`ushort`/`ulong` typedefs,
`<sys/syslog.h>`, `SOL_IPV6`/`SOL_*`, `SUN_LEN`, the `IP_PKTINFO`/`in_pktinfo`
family, `<sys/time.h>` pulling `<time.h>`, `<sys/socket.h>` pulling
`<sys/uio.h>`, and `<ifaddrs.h>` + `getifaddrs`/`freeifaddrs`/`if_nametoindex`/
`if_indextoname`/`getdomainname`.
