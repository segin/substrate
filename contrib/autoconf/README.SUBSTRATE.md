# GNU Autoconf — Substrate port

GNU Autoconf 2.73: `autoconf`, `autoheader`, `autom4te`, `autoreconf`,
`autoscan`, `autoupdate` and `ifnames`, for regenerating configure
scripts natively on substrate.  Needs `/usr/bin/perl` (contrib/perl)
and `/usr/bin/m4` (contrib/m4) at run time; automake and libtool
complete the set.

## Build chain
- `fetch.sh` — download + SHA-verify + extract autoconf-2.73.tar.xz,
  apply the substrate patch series from `series`.
- `build.sh` — Autoconf is perl and shell scripts plus m4 sources, so
  it is built on the host (no `--host`) with the paths the scripts use
  on substrate (`PERL=/usr/bin/perl M4=/usr/bin/m4`), and staged into
  `${SUBSTRATE_TOP}/dist-overlay/dist-autoconf/usr/`.  The host needs
  perl and GNU m4 at those paths; the frozen `autoconf.m4f` is portable
  across GNU m4 1.4.x.

## Patch series
| patch | what |
|---|---|
| 0001 | `config.sub` accepts `substrate`; `config.guess` maps `uname -s` = Substrate to `i386-unknown-substrate`.  These are the copies `autoreconf -i` installs into packages. |

## Testing
Verified on target together with automake and libtool: `autoreconf -fi`
on an automake + libtool project, `configure` (build triple guessed as
i386-unknown-substrate, shared libraries enabled), `make`, `make
install`, and the installed program runs against its installed shared
library.
