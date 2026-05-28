# nginx on substrate

Upstream: https://nginx.org/
Pinned: 1.26.2 (SHA-256 `627fe086209bba80a2853a0add9d958d7ebbdffa1a8467a5784c9a6b4f03d738`)

## Why

A real HTTP/HTTPS server for substrate — static file serving, reverse
proxy, TLS termination.  nginx is small, event-driven, and has minimal
external dependencies, which makes it a good fit for the i386 target.

## The cross-compile problem

nginx's `configure` is a bespoke shell script (not autoconf).  For
feature detection it compiles a probe **and runs it**.  A probe
cross-compiled for substrate is a substrate-i386 ELF that can't
execute on the Linux build host, so:

- `auto/types/sizeof` runs a program that prints `sizeof(type)`; with
  no output it can't detect any size and `configure` dies with
  "can not detect <type> size".
- `auto/feature` run-tests (`ngx_feature_run=yes|value|bug`) report
  "found but is not working" and disable the feature — including the
  shared-memory primitives (`MAP_ANON` / `MAP_DEVZERO` / SysV shm /
  POSIX sem) nginx needs for its shared zones.

### How we drive it

nginx has a built-in `--crossbuild=SYSTEM:RELEASE:MACHINE` switch that
sets `NGX_PLATFORM` directly and skips the `uname` probe.  We pass
`--crossbuild=substrate:1.0:i386`.  Because `substrate:*` matches none
of the OS cases in `auto/os/conf`, nginx takes the generic-Unix path:
no Linux epoll, no BSD kqueue — it uses **select + poll** (both forced
on with `--with-poll_module --with-select_module`).  The `i386`
machine token sets the cache-line / alignment knobs correctly.

Two patches finish the job:

- `0001-cross-build-sizeof-preset.patch` — when `NGX_PLATFORM` is
  `substrate:*`, `auto/types/sizeof` uses the known ILP32 sizes
  (int/long/void\*/size_t = 4, long long = 8, and substrate's 64-bit
  `off_t` / `time_t` = 8) instead of running the probe.
- `0002-cross-build-feature-assume.patch` — `auto/feature` treats a
  successful compile+link of a run-test as "feature present" under
  `substrate:*` (the standard cross-compile assumption).  `value`
  tests define their macro to `${ngx_feature_value:-1}`; `bug` tests
  assume the defect is absent.

## Scope

Enabled:
- `--with-http_ssl_module` (links the staged OpenSSL 3.0.13)
- `--with-http_gzip_static_module` (links the staged zlib 1.3.1)
- `--with-poll_module --with-select_module` (the only event methods
  substrate provides)

Disabled:
- `--without-pcre` / `--without-http_rewrite_module` — no PCRE port
  yet, so regex `location` blocks and `rewrite` are unavailable.
- epoll / kqueue / eventport — not present on substrate; the generic
  `--crossbuild` path never probes for them.

`-Werror` is demoted via `--with-cc-opt=-Wno-error` plus the GCC-16
legacy-C demotions (`-Wno-incompatible-pointer-types`,
`-Wno-int-conversion`, `-Wno-return-mismatch`,
`-Wno-implicit-function-declaration`) — the same set the xorg-server
port needs.  The only one that actually fires is substrate's
`restrict`-qualified `select()` timeval arg.

## Build status

- Builds end-to-end into a substrate-i386 ELF
  (`/usr/sbin/nginx`, DT_NEEDED: libssl/libcrypto/libz/libc/libsys/libdl).
- On target: `nginx -V` reports `nginx/1.26.2` + `OpenSSL 3.0.13` with
  TLS SNI; `nginx -t` validates the default config.
- **Serves HTTP.**  A plain `nginx` (using the shipped config) binds
  `:80`, daemonizes, and answers `GET /` with `200 OK` + the default
  index page over loopback — verified on a freshly-baked rootfs with a
  minimal in-tree TCP client.

### Known limitation: master/worker

nginx's multi-process model does not yet bring up a worker on
substrate.  The master forks but the child never reaches its
`accept()` loop, so a stock master+worker config opens the listen
socket and then never answers (only the master appears in `ps`;
`error.log` stays empty — it dies before logging).  Root cause is on
the substrate side (fork / master-worker channel setup), not nginx.

Workaround shipped as patch 0003: the default `nginx.conf` sets
`master_process off;`, so one process both manages and serves.  This
is fully functional (the serving verification above runs this way).
Drop the directive once substrate's process model supports nginx
workers.

## Layout

    contrib/nginx/
        README.SUBSTRATE.md
        fetch.sh        # download + SHA-256 verify + apply series
        build.sh        # --crossbuild configure + make + stage to dist-nginx
        series
        patches/
            0001-cross-build-sizeof-preset.patch
            0002-cross-build-feature-assume.patch
        build/          # extracted tree (not committed)

## Dependencies

Build `contrib/zlib` and `contrib/openssl` first — `build.sh` points
`-I` / `-L` at `dist-zlib` and `dist-openssl`.  The repo-root
`build.sh` orders `zlib` and `openssl` before `nginx` in
`DEFAULT_CONTRIB`.
