# mc on Substrate

GNU Midnight Commander 4.8.33, the two-panel console file manager with its
built-in viewer, editor (`mcedit`) and diff viewer.

## Dependencies

glib2 (and gmodule), the wide-character ncurses, and e2fsprogs
(`libext2fs`/`libe2p`, for the file-attributes dialog), all staged in the
cross sysroot before this port builds; `build.sh` asserts on each.

## No patches

The port carries no patch series.  Building 4.8.33 needed changes on the
substrate side, made there rather than worked around here:

- ncurses is now the wide-character build.  mc's ncurses backend draws
  shadows with the `cchar_t` API (`mvin_wchnstr`, `getcchar`, `setcchar`,
  `mvadd_wchnstr`) unconditionally, and only `libncursesw` has it.  That in
  turn needed POSIX `tsearch`/`tfind`/`tdelete` in libc.
- `struct stat` stored `st_mode`, `st_nlink`, `st_uid` and `st_gid` as
  `uint16_t`.  `lib/vfs/parse_ls_vga.c` passes `&st->st_mode` to a function
  taking `mode_t *`, which would have written over `st_nlink`.  The fields now
  have their POSIX types.

## Build notes

- Configured `--host=i386-unknown-linux-gnu` with the substrate cross gcc as
  `CC`; `build.sh` stamps `ELFOSABI_SUBSTRATE` (0x40) into every installed
  ELF file, including the helpers under `/usr/libexec/mc`.
- `--with-screen=ncurses`: S-Lang is not ported.
- `--disable-vfs-sftp`: the SFTP VFS needs libssh2, which is not ported.
- `--sysconfdir=/etc`: the system-wide configuration lives in `/etc/mc`.
- `--disable-nls`: no translations are installed.
- **Sysroot include paths.**  The staged `.pc` files carry `prefix=/usr`, so
  `pkg-config --cflags glib-2.0` says `-I/usr/include/glib-2.0`, which the
  compiler reads as the *build host's* directory.  On a machine with glib
  installed that compiles mc against the host's (x86_64, newer) glib headers
  -- the first attempt did, and failed to link on `g_free_sized` and
  `g_string_free_and_steal`, which the staged glib 2.56 does not have.  On a
  clean runner it fails outright.  `build.sh` rewrites `GLIB_CFLAGS`,
  `GMODULE_CFLAGS`, `EXT2FS_CFLAGS` and `E2P_CFLAGS` into the sysroot, passes
  them to configure, and refuses to continue if any `/usr` include survives.
- `LIBS=-ldl`: the staged `libgmodule-2.0.so` calls `dlopen` and friends but
  records no `DT_NEEDED` on `libdl` (glib2 was configured as a linux host,
  where they are in libc), so the executable links `libdl` itself.
