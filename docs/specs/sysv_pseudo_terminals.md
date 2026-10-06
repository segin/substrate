# System V pseudo-terminals

How a program built for UNIX System V/386 gets a pseudo-terminal on
substrate.  The code is `sys/exec/perso/svr4/svr4_tty.c` (with
`svr4_tty.h`), for the `SVR4` and `SVR3` personalities; the terminals
themselves are substrate's own (`sys/drivers/console/pty.c`).  Tested by
`tests/perso/svr4/ptytest.c`.  The companion to
`sysv_streams_transport.md`.

## Two kinds, and which programs take which

**System V's own.**  A pseudo-terminal is a pair of streams.  The master
is the clone device `/dev/ptmx`; the slave is `/dev/pts/N`.  Three library
calls stand between opening one and opening the other:

| Call | What it does underneath |
|---|---|
| `ptsname(fd)` | `ioctl(fd, I_STR, ISPTM)` to ask whether `fd` is a master, then `fstat(fd)`: the slave is `/dev/pts/` followed by the *minor number* of the master's `st_rdev` |
| `grantpt(fd)` | forks and runs the set-id helper `/usr/lib/pt_chmod`, which calls `ptsname()` itself and then `stat`, `chown` and `chmod` on the slave |
| `unlockpt(fd)` | `ioctl(fd, I_STR, UNLKPT)` |

The slave, once open, is a bare stream.  Whoever opened it makes it a
terminal by pushing modules: `ptem` (the terminal emulation), `ldterm`
(the line discipline), and `ttcompat` for a program that uses the 4.3BSD
terminal interface.

**BSD's.**  `/dev/ptyp0` for the master and `/dev/ttyp0` for the slave,
found by trying names in turn.  Dell UNIX and INTERACTIVE UNIX both have
them as well, and the `xterm` each ships takes these first.

Substrate has both natively, under the same names.

## What the personality adds

Substrate's `/dev/ptmx` and `/dev/pts/N` are Unix98 pseudo-terminals: the
slave's number comes from `TIOCGPTN`, it is unlocked with `TIOCSPTLCK`, and
it is a complete terminal from the moment it exists.  So nothing new is
built; the System V way of asking is answered from what is there.

| The program does | The personality does |
|---|---|
| `I_STR`, `ISPTM` on a master | succeeds, and unlocks the slave (below) |
| `I_STR`, `UNLKPT` | unlocks the slave |
| `fstat` on a master | reports a character device whose minor number is the slave's, under major 30 |
| `I_PUSH` of `ptem`, `ldterm` or `ttcompat` on a terminal | succeeds and changes nothing; any other name is `EINVAL` |
| `I_FIND` of one of them on a slave | 1: it is there |
| `I_LOOK` on a slave | `ttcompat` |
| `I_POP` on a slave | succeeds and changes nothing |
| the same on a master | `I_FIND` 0, the rest `EINVAL`: a master has no modules |
| `ttcompat`'s requests on any terminal | carried out on the terminal's `termios` (below) |

`ISPTM` unlocks because of what follows it.  On System V the slave's node
exists from boot, and `grantpt()`'s helper calls `stat` and `chown` on it
before `unlockpt()` has been called.  Substrate creates the node when the
slave is unlocked.  Nothing is given away: the slave is created mode 0620,
owned by whoever opened the master.

## ttcompat

The 4.3BSD terminal interface, as requests `'t' << 8 | n` on a terminal.
Both `xterm`s set the terminal up with these, and failed at the first one
(`TIOCLSET`, "Error 27") without them.

| Request | Structure | Mapped to |
|---|---|---|
| `TIOCGETP`, `TIOCSETP`, `TIOCSETN` | `struct sgttyb` | erase and kill characters; `ECHO`, `CRMOD` (`ICRNL` and `ONLCR`), `TANDEM` (`IXOFF`), `LCASE`, `CBREAK` (no `ICANON`), `RAW` (no `ICANON`, `ISIG`, `OPOST`, 8 bits), `ODDP`/`EVENP`.  The speed reported is always 9600 |
| `TIOCGETC`, `TIOCSETC` | `struct tchars` | `VINTR`, `VQUIT`, `VSTART`, `VSTOP`, `VEOF`, `VEOL` |
| `TIOCGLTC`, `TIOCSLTC` | `struct ltchars` | `VSUSP`, `VREPRINT`, `VDISCARD`, `VWERASE`, `VLNEXT`; there is no delayed-suspend character |
| `TIOCLGET`, `TIOCLSET`, `TIOCLBIS`, `TIOCLBIC` | the local mode word | `LCRTBS`/`LCRTERA` (`ECHOE`), `LPRTERA`, `LCRTKIL`, `LCTLECH`, `LTOSTOP`, `LFLUSHO`, `LPENDIN`, `LNOFLSH`, `LLITOUT` (no `OPOST`), `LDECCTQ` (no `IXANY`), `LPASS8` |
| `TIOCGETD` | | 2, the "new" line discipline; `TIOCSETD` is accepted |
| `TIOCFLUSH` | | `TCFLSH`, both directions |
| `TIOCNOTTY` | | substrate's |
| `TIOCEXCL`, `TIOCNXCL`, `TIOCHPCL`, `TIOCSBRK`, `TIOCCBRK`, `TIOCSDTR`, `TIOCCDTR` | | accepted |

They are answered for any descriptor that is a terminal, whether or not
`ttcompat` was pushed: on substrate the module is always there.

## Four things underneath that had to be right

- **`/dev` is the kernel's.**  A personality's files are looked for under
  its own root first (`/perso/svr4`), and a root built from distribution
  media has a `/dev` of empty directories.  Opening `/dev/ttyp0` worked,
  by falling back to substrate's; listing `/dev` showed the empty one, so
  `ttyname()`, which looks its terminal up by reading `/dev`, found
  nothing and `tty` said "not a tty".  The two personalities now set
  `native_dev`, and `/dev` and everything below it is never looked for
  under the prefix.
- **`/dev/fd/N` is a character device.**  With the real `/dev` visible,
  `ps` -- which walks it for terminal names -- found its own open
  directory as `/dev/fd/N`, took it for a directory, and descended into
  `/dev/fd/N/fd/N/...` for ever.  On System V those entries are always
  character devices; `stat` reports them so.
- **A pseudo-terminal survives a `stat` of its slave.**  This one is in
  the driver, and was wrong for every program.  The slave's last close
  marks the pair dead so that the master reads end-of-file; and `stat` on
  `/dev/pts/N` opens and closes the node, so a `stat` before anyone had
  opened the slave -- `grantpt()`'s, or `ls -l /dev/pts` -- was a "last
  close" that left the master reading end-of-file for good.  Opening the
  slave now makes the pair live again while its master is open.
- **`chmod`, `chown` and `mknod`** in the shared System V calls passed
  substrate's a kernel copy of the path where it takes the user's, and
  failed with `EFAULT` every time.  `pt_chmod` is the first thing that
  minded.

## What works

On the i386 and x86-64 kernels:

- `tests/perso/svr4/ptytest.c`, a freestanding Release 4 program, takes a
  pseudo-terminal the System V way -- `/dev/ptmx`, `ISPTM`, the master's
  minor number, Dell UNIX's own `/usr/lib/pt_chmod`, `UNLKPT`, the slave,
  the three modules -- and passes data across it both ways: 26 checks.  It
  runs without a vendor's tree as well, skipping the helper.
- Dell UNIX's `xterm` (X11R5) and INTERACTIVE UNIX's (X11R4) open a
  window on substrate's X server and run a shell in it.  In both, `tty`
  answers `/dev/ttyp0`; in Dell's, `stty -a` reports the terminal.

## What does not

- **The lock.**  Between `ptsname()` and `unlockpt()` the slave can be
  opened, by its owner.
- **Real modules.**  Popping `ldterm` does not give a raw stream.
- **Packet mode** (`pckt`), and the master-side `TIOCREMOTE` and
  `TIOCSIGNAL` of System V.
- **Speeds.**  `sgttyb` reports 9600 and setting another is ignored.
- **`ps`.**  It finishes now, but lists no processes: it reads them from
  `/proc` in System V's format.  INTERACTIVE's stops with "ftw() failed"
  walking `/dev`.
