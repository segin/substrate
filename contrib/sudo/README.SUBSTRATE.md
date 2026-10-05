# sudo

Todd Miller's `sudo` (<https://www.sudo.ws/>): run a command as another
user, as `/etc/sudoers` permits.

Upstream: <https://www.sudo.ws/dist/>
Pinned version: **sudo-1.9.17p2**
License: ISC (see `build/sudo-<ver>/LICENSE.md`).
Substrate vendoring: tarball only -- there is no patch series.

## Build

```
./fetch.sh
./build.sh
```

Needs `contrib/zlib` staged (I/O log compression).  Produces, under
`dist-overlay/dist-sudo/`:

- `/usr/bin/sudo` (and the `sudoedit` link), `sudoreplay`, `cvtsudoers`
- `/usr/sbin/visudo`
- `/usr/libexec/sudo/` -- the optional `group_file`, `system_group` and
  `audit_json` plugins
- `/etc/sudoers`, `/etc/sudoers.d/`, `/etc/sudo.conf`
- the manual pages, sections 1, 5 and 8

`build-rootfs.sh` makes `/usr/bin/sudo` setuid root and gives
`/etc/sudoers` (0440) and `/etc/sudoers.d` (0750) the modes sudo insists
on; the staging tree itself is written by an unprivileged build.

## How it is configured

- **Authentication** is substrate's own: `/etc/shadow` through
  `getspnam(3)` and `crypt(3)`.  There is no PAM.
- **`--enable-static-sudoers --disable-shared-libutil`, system zlib.**
  By default sudo installs `libsudo_util.so` and a private libz in
  `/usr/libexec/sudo` and finds them through `DT_RUNPATH`, which
  substrate's `ld.so` does not implement.  Built this way the sudoers
  policy is part of the `sudo` binary and everything it needs is in
  `/usr/lib`.
- **`-lregex` in `LDFLAGS`.**  `regcomp(3)` is in `libregex`, not libc.
  It cannot go in `LIBS`: configure passes that to the build machine's
  compiler too.
- **Left out:** LDAP, SSSD, Kerberos, SELinux, AppArmor, audit, the log
  server and client, OpenSSL, Python plugins, NLS, sendmail, and the
  `noexec` and `intercept` shims (both work by `LD_PRELOAD`).

## The shipped policy

The upstream default: `root` may run anything, and `/etc/sudoers.d/` is
read.  No other user can use sudo until an administrator says so, for
example with `visudo -f /etc/sudoers.d/wheel`:

```
%wheel ALL=(ALL:ALL) ALL
```

## What the port needed from substrate

Nothing in sudo is patched; what it tripped over was fixed where it was
wrong.

- `crypt(3)` was declared only in `<crypt.h>`.  POSIX puts it in
  `<unistd.h>`, which now declares it as well.
- libc had no `getusershell(3)`.  sudo then builds its own but exports
  only one of the three functions from its utility library, and its
  plugins fail to link.  libc now has `getusershell`, `setusershell` and
  `endusershell`.
- libc's `setreuid(2)`/`setregid(2)` were stand-ins that refused to set
  the real and effective ids to different values, `setegid(2)` changed
  the real gid as well, and `setresuid(2)`/`setresgid(2)` did not exist
  -- every way a setuid program drops and regains privilege.  The kernel
  implemented all of them already; they now have native system call
  numbers and libc calls them.
- `poll(2)` on `/dev/tty` never reported the terminal ready.  sudo runs
  the command on a pty and relays its output through `/dev/tty`, and
  waited forever to write the first byte.

## Testing

Run sudo from a real terminal session (a login, ssh, an xterm): it
relays through the caller's controlling terminal, so a harness whose
stdout is not that terminal sees the command's output go elsewhere.
