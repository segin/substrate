# GNU inetutils on substrate

Upstream:  https://www.gnu.org/software/inetutils/
Pinned:    2.5  (released 2023-12-30)
Tarball:   `https://ftp.gnu.org/gnu/inetutils/inetutils-2.5.tar.xz`
SHA-256:   `87697d60a31e10b5cb86a9f0651e1ec7bee98320d048c0739431aac3d5764fb6`
           (verified against PGP signature signed by Simon Josefsson,
            key `A3CC9C870B9D310ABAD4CF2F51722B08FE4745A2`)

## Why

Substrate's in-tree `bin/telnet` and `sbin/telnetd` are stripped-down
implementations that hit the major use cases (cooked vs raw mode,
IAC negotiation, PTY allocation, login chain) but lack the full RFC
855 / RFC 1184 feature set: variable-mode line editing, full set of
IAC `send` subcommands, `set` and `unset` for every option, AUTH
machinery hooks (no-op without Kerberos), full ENVIRON / NEW-ENVIRON
plumbing, tracing, terminal-type cycling, status/break/synch handling.

The inetutils suite ships canonical implementations of those
behaviours.  We import the source, patch where needed for the
substrate target, build the client + server programs we want, and
drop everything else.

## Scope

Built and installed:

- `telnet`   → `/usr/bin/telnet`
- `telnetd`  → `/usr/sbin/telnetd`  (Tentative — substrate's
  `sbin/telnetd` is purpose-built for our PTY layout and may stay
  alongside, with the inetutils binary as `telnetd.inetutils` for
  comparison.)
- `ftp`      → `/usr/bin/ftp`        (canonical RFC 959 client)
- `ftpd`     → `/usr/sbin/ftpd`      (canonical RFC 959 server)

Disabled at configure time:

- rcp, rlogin, rsh, rshd, talk, talkd, tftp, tftpd,
  rexec, rexecd, syslogd, inetd, traceroute, hostname, uucpd,
  whois, dnsdomainname, ifconfig, logger, ping, ping6, traceroute6

Encryption + authentication ARE built (`--enable-authentication` +
`--enable-encryption`).  `contrib/openssl` provides libcrypto/libssl
for the bits inetutils can pick up; Kerberos itself isn't shipped
yet (`--without-krb5`), so the krb-backed AUTH paths are dormant
until a future `contrib/krb5` is added.  The OpenSSL pkg-config
metadata staged by `contrib/openssl/build.sh` is discovered via
`PKG_CONFIG_PATH`.

## Layout

    contrib/inetutils/
        README.SUBSTRATE.md   ← this file
        fetch.sh              ← download + extract + patch
        build.sh              ← configure + build + install
        series                ← patch order
        patches/              ← substrate-specific changes
        build/                ← extracted source + build dir (NOT vendored)

`fetch.sh` is idempotent; running it from a clean tree downloads the
tarball, verifies the SHA-256, extracts under `build/`, and applies
every patch in `series` in order.  `build.sh` invokes configure with
the substrate target triple and the disable list above, then runs
`make` and a staged `make install` into `${DESTDIR}` for the rootfs
overlay to pick up.
