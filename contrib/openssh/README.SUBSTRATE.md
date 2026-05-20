# OpenSSH on substrate

Upstream:  https://www.openssh.com/
Pinned:    10.0p2  (released 2025-04-09)
Tarball:   `https://cdn.openbsd.org/pub/OpenBSD/OpenSSH/portable/openssh-10.0p2.tar.gz`
SHA-256:   `021a2e709a0edf4250b1256bd5a9e500411a90dddabea830ed59cef90eb9d85c`

## Scope

Built and installed:

- `ssh`         → `/usr/bin/ssh`         (client)
- `sshd`        → `/usr/sbin/sshd`       (server)
- `ssh-keygen`  → `/usr/bin/ssh-keygen`  (host + user key tool)
- `ssh-add`     → `/usr/bin/ssh-add`     (agent helper)
- `ssh-agent`   → `/usr/bin/ssh-agent`   (auth agent)
- `scp`         → `/usr/bin/scp`         (file copy via ssh)
- `sftp`        → `/usr/bin/sftp`        (interactive file transfer)
- `sftp-server` → `/usr/libexec/sftp-server`  (SFTP subsystem)
- `ssh-keyscan` → `/usr/bin/ssh-keyscan` (host-key probe)
- `ssh-keysign` → `/usr/libexec/ssh-keysign` (host-based auth helper)
- `ssh-pkcs11-helper`, `ssh-sk-helper` → `/usr/libexec/`
- `moduli`      → `/etc/ssh/moduli`       (Diffie-Hellman group params)

Configure flags (set in `build.sh`):

- `--without-pam`         per project directive — substrate has no PAM stack.
                          sshd talks to `/etc/shadow` directly via `crypt(3)`.
- `--without-x`           per project directive — no X11 forwarding.
- `--without-selinux`     no SELinux on substrate.
- `--without-kerberos5`   no krb5 port yet.
- `--without-libedit`     no command-line editing in `sftp`.  Substrate
                          ships `libedit` but configure-probes that
                          aren't worth the libedit-on-cross-build risk
                          right now; flip back when sftp interactive
                          editing becomes important.
- `--with-zlib=…`         points at the `contrib/zlib` stage.
- `--with-ssl-dir=…`      points at the `contrib/openssl` stage.

## Dependencies

- contrib/openssl  — libssl/libcrypto for all crypto primitives.
- contrib/zlib     — DEFLATE compression on the wire (Compression yes).
- substrate libpwdb (lib/pwdb/) is the de-facto `getpwnam`/`crypt`
  backend that the no-PAM build path falls into.

## Runtime expectations

- `/etc/ssh/` must exist and hold the host keys.  `ssh-keygen -A`
  on first boot generates them; provision via an `rc.d` hook or
  bake the keys into the image via `build-rootfs.sh`.
- `/var/empty/` must exist and be uid=0 / mode 0755 for privsep
  `chroot()` to succeed.  Create as part of `--image`.
- `getpass(3)` is used for password prompts on the client side.
  Substrate's libc impl is in `lib/c/src/posix_extra3.c`.

## Layout

    contrib/openssh/
        README.SUBSTRATE.md   ← this file
        fetch.sh              ← download + verify + extract + patch
        build.sh              ← configure + build + install
        series                ← patch order
        patches/              ← substrate-specific changes (none yet)
        build/                ← extracted source + build dir (NOT vendored)

`fetch.sh` downloads the upstream tarball, verifies SHA-256, extracts
under `build/openssh-10.0p1/` (yes, the p2 tarball extracts as
p1 — upstream packaging quirk), and applies every patch in `series`.
`build.sh` runs configure with the cross triple, builds, and
installs into `${DESTDIR}` (default `${SUBSTRATE_TOP}/dist-openssh`)
for the rootfs overlay.
