# GNU Automake — Substrate port

GNU Automake 1.18.1: `automake` and `aclocal`, with their support files
under `/usr/share/automake-1.18` and `/usr/share/aclocal-1.18`.  Needs
`/usr/bin/perl` (contrib/perl) and autoconf (contrib/autoconf) at run
time.

## Build chain
- `fetch.sh` — download + SHA-verify + extract automake-1.18.1.tar.xz,
  apply the substrate patch series from `series`.
- `build.sh` — perl scripts and data only, built on the host (no
  `--host`) with `PERL=/usr/bin/perl` and staged into
  `${SUBSTRATE_TOP}/dist-overlay/dist-automake/usr/`.  configure checks
  the host's autoconf (>= 2.65).

## Patch series
| patch | what |
|---|---|
| 0001 | `lib/config.sub` accepts `substrate`; `lib/config.guess` maps `uname -s` = Substrate to `i386-unknown-substrate`.  These are the copies `automake --add-missing` installs into packages. |

## Testing
See contrib/autoconf/README.SUBSTRATE.md: `autoreconf -fi`, configure,
make and install of an automake + libtool project on target.
