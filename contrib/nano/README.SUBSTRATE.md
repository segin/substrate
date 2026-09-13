# nano on Substrate

GNU nano 9.2, the small terminal text editor.

## Dependencies

ncurses (screen), zlib and libmagic (`contrib/file`), all staged in the
cross sysroot before this port builds; `build.sh` asserts on each.

## No patches

The port carries no patch series.  Building 9.2 exposed four gaps on the
substrate side, each fixed there rather than worked around here:

- `<wchar.h>` included `<stdint.h>` and `<stdio.h>`.  gnulib's replacement
  `<stdint.h>` includes `<wchar.h>` partway through, the re-entrant include
  returned nothing, and `wint_t`/`mbstate_t` vanished.
- libc had no `__fseterr()`, so gnulib compiled `fseterr.c`, which stops with
  `#error "Please port gnulib fseterr.c to your platform!"`.
- `struct stat` had no `st_atim`/`st_mtim`/`st_ctim`; `src/files.c` uses them
  unconditionally when copying timestamps onto a backup file.
- ncurses was configured with `NCURSES_ENABLE_STDBOOL_H 0`, so `curses.h`
  redefined `bool` as `unsigned char` under nano's own `<stdbool.h>`.

## Build notes

- Configured `--host=i386-unknown-linux-gnu` with the substrate cross gcc as
  `CC`; `build.sh` stamps `ELFOSABI_SUBSTRATE` (0x40) into the installed
  executables.
- `--sysconfdir=/etc` puts the global `nanorc` at `/etc/nanorc`.
- `--disable-nls`: no translations are installed.

## Known limitation

The screen library is narrow ncurses, so nano runs without multibyte (UTF-8)
editing until ncurses is rebuilt with wide-character support.
