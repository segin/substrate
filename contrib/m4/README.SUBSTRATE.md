# GNU m4 — Substrate port

GNU m4 1.4.21 cross-built for substrate via the stage-1 GNU toolchain.
Installed as `/usr/bin/m4`; flex, autoconf and bison run it at build
time.

## Build chain
- `fetch.sh` — download + SHA-verify + extract m4-1.4.21.tar.xz, apply
  the substrate patch series from `series`.
- `build.sh` — `./configure --host=i386-unknown-substrate --disable-nls
  --disable-threads` + `make` + stage into
  `${SUBSTRATE_TOP}/dist-overlay/dist-m4/usr/`.

## Patch series
| patch | what |
|---|---|
| 0001 | `config.sub`: accept `substrate` as an OS name. |
| 0002 | gnulib `fpending`/`fpurge`/`freading`/`freadahead`: a `__substrate__` branch over Substrate's `FILE` (write data in `buffer..pos`, read data in `pos..limit`, `rw_state` 1 = reading / 2 = writing, `has_unget` for an `ungetc` byte). |
| 0003 | gnulib `getlocalename_l-unsafe`: Substrate declares `locale_t` but has no `newlocale` or `LC_GLOBAL_LOCALE`; only the C locale exists, so every query answers `"C"`. |

## Testing
Verified on target: macro definition and expansion, `eval`, `ifdef`,
`translit`, `regexp`, `patsubst`, `format`, `esyscmd`, diversions,
`include`, string builtins, frozen state (`-F` / `-R`), and `m4exit`
status.  m4's own `make check` needs to run on the target and is not
wired up for the cross build.
