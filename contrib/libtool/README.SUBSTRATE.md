# GNU Libtool — Substrate port

GNU Libtool 2.6.2: `libtoolize`, the `libtool` script, the libtool
autoconf macros in `/usr/share/aclocal`, and libltdl (`libltdl.so.7`,
`libltdl.a`, `ltdl.h`).

## Build chain
- `fetch.sh` — download + SHA-verify + extract libtool-2.6.2.tar.xz,
  apply the substrate patch series from `series`.
- `build.sh` — cross build (`--host=i386-unknown-substrate`), since
  libltdl is a target library and the installed `libtool` is configured
  for the target compiler.  Staged into
  `${SUBSTRATE_TOP}/dist-overlay/dist-libtool/usr/`.

The installed `libtool` and `libtoolize` record the tools found at
configure time, so build.sh makes them substrate's:
- cache variables: `ac_cv_path_GREP=/bin/grep` (without it libtoolize
  cannot read `AC_CONFIG_AUX_DIR`), `lt_cv_truncate_bin="/usr/bin/sed
  -e 4q"` (substrate has no dd), and `lt_cv_sys_lib_dlsearch_path_spec`
  (otherwise the build host's `/etc/ld.so.conf` is read);
- after install, the cross tool names and paths in `libtool` are
  rewritten to the native ones (`i386-unknown-substrate-gcc`/`g++`,
  unprefixed binutils, crt objects in `/usr/lib`), and the build fails
  if any build-host path is left.

## Patch series
| patch | what |
|---|---|
| 0001 | `build-aux/config.sub` accepts `substrate`; `config.guess` maps `uname -s` = Substrate to `i386-unknown-substrate`.  These are the copies `libtoolize --install` installs into packages. |
| 0002 | `m4/libtool.m4`: `substrate*` joins the `linux*` arms (dynamic linker, deplibs check, C and C++ PIC flags, shared-library link commands), so packages autoreconf'd on substrate build shared libraries.  build.sh keeps the file's tarball timestamp and runs `contrib/substrate-libtool-shared.sh` on the shipped configure scripts instead of regenerating them. |

## Testing
On target, an automake + libtool project builds `libgreet.so.1.0.0`
with the right SONAME, installs, and the installed program runs.

## Known gaps
- libtool's wrapper for uninstalled programs (`./prog` in the build
  tree) sets `LD_LIBRARY_PATH`, which substrate's ld.so does not honour
  yet, so such programs only run once installed.
