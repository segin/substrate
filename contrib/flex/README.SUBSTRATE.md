# flex — Substrate port

flex 2.6.4 cross-built for substrate via the stage-1 GNU toolchain.
Installs `/usr/bin/flex` (plus `lex` and `flex++`), `libfl`
(`.a` and `.so.2`) and `FlexLexer.h`.  flex runs `/usr/bin/m4`
(`contrib/m4`) when it generates a scanner.

## Build chain
- `fetch.sh` — download + SHA-verify + extract flex-2.6.4.tar.gz, apply
  the substrate patch series from `series`.
- `build.sh` — `configure --host=i386-unknown-substrate` + `make` +
  stage into `${SUBSTRATE_TOP}/dist-overlay/dist-flex/usr/`.

## Configure notes
- `--disable-bootstrap`: the bootstrap scanner `stage1flex` is a host
  program, but it links with `LIBS`, and `-lregex` exists only for the
  target.  The tarball ships a pregenerated `scan.c`.
- `LIBS=-lregex`: substrate keeps `regcomp`/`regexec` in libregex.
- `ac_cv_path_M4=/usr/bin/m4`: otherwise the host's m4 path is baked in.
- `ac_cv_func_{malloc,realloc}_0_nonnull=yes`: true on substrate; the
  cross default pulls in `rpl_malloc` and breaks the build.
- `contrib/substrate-libtool-shared.sh` lets libtool build `libfl.so`.

## Patch series
| patch | what |
|---|---|
| 0001 | `config.sub`: accept `substrate` as an OS name. |

## Testing
Verified on target: a C scanner (`flex` + `gcc`), `lex` with `-lfl`
supplying `main`/`yywrap`, and a C++ scanner (`flex -+` + `g++`).
flex hung on every run until libc's `fseek` stopped setting the error
indicator when a seek fails with `ESPIPE`.
