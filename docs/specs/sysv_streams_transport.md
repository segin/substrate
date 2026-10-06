# The System V STREAMS transport shim

How programs built for UNIX System V/386 Release 3 and Release 4 reach the
network, and the X server in particular, on substrate.  The code is
`sys/exec/perso/svr4/svr4_streams.c` (with `svr4_streams.h`); it serves the
`SVR4` and `SVR3` personalities, which share their system calls
(`svr4_calls.c`).  Tested by `tests/sys/host_test_svr4_streams.c`.

## The problem

System V has no `socket(2)`.  Networking is done through STREAMS: a program
opens a *transport provider* device, optionally pushes *modules* onto the
stream, and exchanges *Transport Provider Interface* (TPI) messages with
the provider using `putmsg(2)` and `getmsg(2)` and a family of `ioctl(2)`
requests.  Two libraries hide this:

- **`libnsl`** -- the Transport Layer Interface: `t_open()`, `t_bind()`,
  `t_connect()`, `t_snd()`, `t_rcv()`.  Release 3 and Release 4.
- **`libsocket`** -- Release 4's BSD socket calls.  `socket()` and
  `connect()` are library functions here, written on the same messages,
  with the `sockmod` module pushed in place of `timod`.

The X library sits on one or the other, and adds transports of its own for
local connections, which are streams too.  None of this is unusual at the
wire: once connected, an X client from 1990 sends the bytes an X server
expects today.  What was missing was everything before the first byte.

An X client on either system failed at its first `open(2)`:

| System | `DISPLAY` | What it opened | Result |
|---|---|---|---|
| Dell UNIX 2.2 (SVR4, X11R5) | any | `/dev/ticotsord` or `/dev/tcp`, named by `/etc/netconfig` | `ENOENT` |
| INTERACTIVE UNIX 3.0 (SVR3, X11R4) | `:0`, `unix:0` | `/dev/spx` | `ENOENT` |
| INTERACTIVE UNIX 3.0 | `host:0` | `/dev/X/nameserver` | `ENOENT` |

## The approach

The shim is the provider and the modules, for a client.  Opening one of
the devices creates a substrate socket and returns its descriptor; the
shim keeps a small record (a *stream*) for it, found again by the socket's
node, so the same record serves a `dup(2)` or a child after `fork(2)`.
The TPI requests are carried out with the socket and answered the way a
provider answers.  After the connection is made, `read(2)` and `write(2)`
on the descriptor are the socket's own and the shim is out of the path.

This is the method the Linux iBCS2 emulator used for the same binaries.
It is not a STREAMS implementation: there are no queues, no real modules,
and no multiplexing.

The hooks in `svr4_call()`:

| Call | Number | What the shim does |
|---|---|---|
| `open` | 5 | `svr4_streams_open()`: a device name makes a stream; anything else is the filesystem's |
| `close` | 6 | forgets the stream when the last descriptor goes |
| `read` | 3 | on a stream, returns `EAGAIN` rather than 0 when there is nothing (below) |
| `ioctl` | 54 | `svr4_streams_ioctl()`: the `I_*`, `TI_*` and socket requests |
| `getmsg`, `putmsg` | 85, 86 | TPI messages |
| `poll` | 87 | substrate's, with the event bits translated |
| `readv`, `writev` | 121, 122 | a vector at a time |

`hrtsys` (109) was added alongside: it is how Release 4's libc reads the
time of day, and the X Toolkit computes every timeout with it.

## Devices

| Path | Socket | Kind |
|---|---|---|
| `/dev/tcp` | `AF_INET`, stream | provider |
| `/dev/udp` | `AF_INET`, datagram | provider |
| `/dev/ticotsord`, `/dev/ticots` | `AF_UNIX`, stream | provider (the "loopback" transports) |
| `/dev/ticlts` | `AF_UNIX`, datagram | provider |
| `/dev/spx` | `AF_UNIX`, stream, unconnected | one end of a stream pipe |
| `/dev/XNR`, `/tmp/.X11-unix/XN` | `AF_UNIX`, stream, unconnected | the X server's request stream |
| `/dev/X/server.N` | `AF_UNIX`, stream, connected to display N | a named stream |

None of these is a node in the filesystem; the names are matched in
`open(2)`.  The X server is always the one at `/tmp/.X11-unix/XN`, which
is looked up in substrate's own root, not under `/perso`.

## Requests on a stream

**Modules.**  `I_PUSH` accepts `sockmod`, `timod` and `tirdwr` and records
the name; `I_POP`, `I_LOOK` and `I_FIND` answer from that record.  The
modules do nothing: what they would translate is handled directly.

**`I_STR`** carries a module's request in a `struct strioctl`.  The answer
replaces the request in the caller's buffer.

| `ic_cmd` | From | Answer |
|---|---|---|
| `SI_GETUDATA` | `sockmod` | `struct si_udata`: TIDU size, address size, option size, service type.  Seven words, eight if the caller's buffer has room -- Release 4.0's `libsocket` passes 28 bytes, and 32 written into it ran over its stack |
| `TI_GETINFO` | `timod` | `T_INFO_ACK` |
| `TI_BIND` | `timod` | `T_BIND_ACK` with the address asked for, or a null one of the family |
| `TI_UNBIND` | `timod` | `T_OK_ACK` |
| `TI_OPTMGMT` | `timod` | `T_OPTMGMT_ACK` echoing the options: what was asked for is reported as in force |
| `SI_SHUTDOWN` | `sockmod` | `shutdown(2)` |

The four `TI_` requests are numbered from 100 in Release 3 and from 140 in
Release 4; both are accepted.  A refusal by the provider is not a failed
`ioctl`: as `timod` does, the shim returns it as the call's value, the
errno in the second byte and the `t_errno` in the first.

**Others.**  `I_NREAD` and `FIONREAD` give the bytes waiting (the X library
sizes its reads by one or the other); `FIONBIO` sets non-blocking mode;
`TI_GETMYNAME` and `TI_GETPEERNAME` fill a `netbuf` from the socket;
`I_SETSIG`, `I_SRDOPT`, `I_SWROPT`, `I_SETCLTIME` and `I_FLUSH` succeed
and do nothing; `I_CANPUT` says yes.

## Messages

`putmsg(2)` with a control part is a TPI request:

| Request | Effect | Queued for `getmsg(2)` |
|---|---|---|
| `T_CONN_REQ` | `connect(2)` to the address, waited for | `T_OK_ACK`, then `T_CONN_CON` -- or `T_DISCON_IND` with the errno as the reason |
| `T_BIND_REQ`, `T_UNBIND_REQ`, `T_INFO_REQ`, `T_OPTMGMT_REQ` | as the `ioctl` forms | the acknowledgement |
| `T_DISCON_REQ` | `shutdown(2)`, both ways | `T_OK_ACK` |
| `T_ORDREL_REQ` | `shutdown(2)`, writing | nothing |
| `T_DATA_REQ`, or no control part | the data part is written | nothing |
| anything else | | `T_ERROR_ACK`, `TNOTSUPPORT` |

Up to four answers wait on a stream.  `getmsg(2)` returns the oldest;
acknowledgements are priority messages and come back with `RS_HIPRI`, and
a caller that asks for priority messages only is not given a `T_CONN_CON`.
With nothing waiting it reads from the socket and returns the bytes as a
data part; the first end-of-file is reported as a `T_ORDREL_IND`.

The connection is made synchronously inside `putmsg(2)`, with the socket
switched to blocking for the duration, because a provider acknowledges a
connection request before the program looks for the acknowledgement.

**Addresses.**  An Internet address is a `struct sockaddr_in` on both
sides.  A local one is what Release 4's `libsocket` sends for `AF_UNIX`: a
`sockaddr_un` followed by the device and inode numbers `stat(2)` gave for
the path.  The shim uses the path and ignores the rest.  A loopback
address that is not a path -- a name agreed between two System V programs
-- has no socket to go to and is refused (`TBADADDR`).

## Three things outside the shim that had to be right

- **A socket's file is a FIFO.**  Release 4's `bind()` makes the file with
  `mknod(path, S_IFIFO)`, and its `connect()` calls `stat(2)` and fails
  with `ENOTSOCK` unless the mode is exactly `S_IFIFO`, no permission
  bits.  `svr4_put_xstat()` reports a substrate socket file that way.
- **`read(2)` with nothing to read.**  The shared System V `read` returns
  0 for `EAGAIN`, which is right for a pipe or a terminal under
  `O_NDELAY`.  A stream that is not a terminal fails with `EAGAIN`
  instead, and on a connection 0 means the peer closed: the X library took
  the first empty read for a dead server.
- **`poll(2)`.**  `select()` in both systems' libc is `poll`.  The
  structure is substrate's; `POLLRDNORM` is answered as `POLLIN`, and
  Release 4's `POLLWRBAND` is where substrate has `POLLWRNORM`.

## The local X transports

**Release 4, `/dev/X/server.N`.**  A named stream: opening it is a
connection.  The shim connects a socket to display N before `open(2)`
returns, and fails the open with `ENXIO` if no server is listening.  (Dell's
X11R5 does not use it -- it calls `socket()` with `AF_UNIX` -- but USL's
own X library does.)

**Release 3, `/dev/spx`.**  `/dev/spx` is the stream pipe driver: each open
is one end of a pipe with nothing at the other.  The server holds a
*request stream* open, and a client gets one end of a new pipe to it.  Two
conventions exist, and both are handled:

- *SCO.*  The client opens the request stream `/dev/XNR` and one
  `/dev/spx`, and does `I_FDINSERT` on the request stream naming the
  `/dev/spx` descriptor.  It then talks over that descriptor.
- *INTERACTIVE.*  The client opens `/dev/spx` twice, joins the two with
  `I_FDINSERT` on one naming the other, opens the request stream -- which
  INTERACTIVE mounts at `/tmp/.X11-unix/XN` -- and sends it the first with
  `I_SENDFD`.  It closes that one and talks over the second.

In the shim a `/dev/spx` is an unconnected local socket.  `I_FDINSERT`
between two of them records that they are a pair; `I_SENDFD` or
`I_FDINSERT` on a request stream connects the end the client keeps to
display N.  Nothing is ever sent to the server out of band, since
substrate's X server is not waiting on a request stream: it sees an
ordinary connection.

Opening `/tmp/.X11-unix/XN` with `open(2)` is, for anything else, an open
of a socket's file, which no program can use; so treating it as the
request stream takes nothing away.

## What works

Against substrate's `Xfbdev`, on both the i386 and the x86-64 kernel:

| Clients | `:0` | `unix:0` | `127.0.0.1:0`, `localhost:0` |
|---|---|---|---|
| Dell UNIX 2.2, X11R5, dynamically linked against `libX11.so.5.0` and `libsocket.so` | yes | yes | yes |
| INTERACTIVE UNIX 3.0, X11R4, COFF with `/shlib/libX11_s` | yes | yes | no |

`xdpyinfo`, `xlsclients`, `xwininfo`, `xclock`, `xeyes`, `xlogo`, `xcalc`
and (INTERACTIVE) `xload` were run; several at once draw and update
correctly.  `xterm` runs a shell on both; what it needs besides a
connection is in `sysv_pseudo_terminals.md`.

## What does not

- **A server's half.**  `T_CONN_IND` and `T_CONN_RES`, `SI_LISTEN`, and
  receiving a descriptor (`I_RECVFD`) are not there.  Nothing can listen
  through the shim.
- **INTERACTIVE's clients over TCP.**  Before it opens `/dev/tcp`, its X
  library opens `/dev/X/nameserver`, a stream to a daemon of INTERACTIVE's
  that turns a host name into a transport address, and speaks a protocol
  of its own to it.  That daemon is not emulated.  (TLI on `/dev/tcp` is:
  a program that calls `t_connect()` with an address it has is served.)
- **Datagrams.**  `/dev/udp` and `/dev/ticlts` open, but `T_UNITDATA_REQ`
  is refused.
- **Explicit binds.**  A `T_BIND_REQ` is acknowledged with the address it
  named and the socket is left to bind itself; a client that needs a
  particular local port does not get it.
- **Non-blocking connects.**  The connection is always waited for.
- **Stale records.**  A stream's record is dropped at the last `close(2)`.
  A process that exits without closing leaves it until the socket's node
  is reused; a record found for a newly created socket is reset, so this
  costs a table slot (of 64), never a wrong answer.

## Tracing

`debug=perso:svr4:streams` on the kernel command line prints every TPI
request, every answer read, and every `I_STR` with its command, in
hexadecimal, alongside `debug=perso:svr4:syscall` or
`debug=perso:svr3:syscall`.  That, and disassembling the vendor's library
where the trace stopped, is how the sequences above were found.
