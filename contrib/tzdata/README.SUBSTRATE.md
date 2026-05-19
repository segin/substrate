# tzdata on substrate

Upstream:  https://www.iana.org/time-zones
Pinned:    2024a   (released 2024-02-01)
Tarballs:  `https://data.iana.org/time-zones/releases/tzdata2024a.tar.gz`
           `https://data.iana.org/time-zones/releases/tzcode2024a.tar.gz`
SHA-256:   tzdata:  `0d0434459acbd2059a7a8da1f3304a84a86591f6ed69c6248fffa502b6edffe3`
           tzcode:  `e23f4f50cb27e9c7fb1f9c7a1ee1c2ce8a6097935adc02ddd64b9d9d39c0fa9d`

## Why

We need the IANA tz database and the `zic`/`zdump` compiler to ship
real /etc/zoneinfo data on the image.  Without it, `tzset(3)` falls
back to UTC for every TZ value, `localtime(3)` lies about wall-clock
time, and tools that expect /usr/share/zoneinfo/$TZ (mutt, dovecot,
cron, every libc that consults `TZif` v2 files) fail silently.

## Scope

- `zic` and `zdump` cross-built for substrate using stage-1 toolchain.
- Compiled timezone binaries staged into
  `${DESTDIR}/usr/share/zoneinfo/{Africa,America,Antarctica,Arctic,
  Asia,Atlantic,Australia,Europe,Indian,Pacific,Etc,…}` plus the
  canonical `/etc/localtime` (defaults to `UTC`; users can `ln -sf`
  whatever zone they want).
- `tzdata.zi` consolidated rule file for newer libc's that read it
  directly.

## Layout

    contrib/tzdata/
        README.SUBSTRATE.md
        fetch.sh        ← grabs BOTH tzdata + tzcode, verifies, extracts
        build.sh        ← cross-builds zic, runs zic to compile zones
        series          ← patch order (empty by default)
        patches/
        build/          ← extracted source + compiled zones (NOT vendored)
