# curl on substrate

Upstream:  https://curl.se/
Pinned:    8.23.0
Tarball:   `https://curl.se/download/curl-8.23.0.tar.xz`
SHA-256:   `8dadd7d2536a314f982ad05a651b713f426bf7d02cc2c76cffb9796f2218e6c5`

## Why

Substrate has no HTTP/HTTPS/FTP/SFTP client.  curl gives us:

- `curl(1)` — the swiss-army knife CLI for HTTP(S), FTP(S), SMTP(S),
  IMAP(S), POP3(S), MQTT, file://, gopher, dict, telnet, tftp.
- `libcurl.so.4` — the C API the rest of the world links against
  for HTTP work (git smart-http, package managers, news fetchers,
  rsync-over-http, etc.).

`contrib/openssl` provides the TLS/crypto backend.  libpsl, libnghttp2,
libssh, libidn2, brotli, zstd are all upstream-optional and disabled
by default until we have contrib packages for them; the build still
gets HTTP/1.1 + TLS 1.2/1.3 + FTP(S) + file:// + the common auth
schemes (Basic, Digest, NTLM, AWS-SigV4) out of the box.

## Scope

Built and installed:

- `/usr/bin/curl`
- `/usr/lib/libcurl.so.4{,.8.0}`
- `/usr/lib/libcurl.a`
- `/usr/include/curl/*.h`
- `/usr/lib/pkgconfig/libcurl.pc`

Enabled at configure time:

- OpenSSL backend via `--with-openssl=${OPENSSL_STAGE}/usr`
- Protocols: HTTP, HTTPS, FTP, FTPS, FILE, TFTP, GOPHER, DICT,
  TELNET, SMTP, SMTPS, POP3, POP3S, IMAP, IMAPS

Disabled (no contrib yet):

- libssh2 (SFTP/SCP), libpsl, libidn2, libnghttp2 (HTTP/2),
  brotli, zstd, libgsasl, libldap, librtmp.  Re-enable when their
  contrib packages land.

## Layout

    contrib/curl/
        README.SUBSTRATE.md
        fetch.sh
        build.sh
        series
        patches/
        build/    ← NOT vendored
