# `strip` — shipped by GNU binutils

substrate's `/usr/bin/strip` comes from the **GNU binutils**
stage-2 build (`contrib/binutils/`).  `build-rootfs.sh`'s
`install_to_dist()` overlays `dist-toolchain/usr/bin/strip`
into the rootfs at `/usr/bin/strip`.

This directory exists only so the recursive
`make -C usr.bin` SUBDIRS iteration doesn't break.  The Makefile
here is intentionally empty — no per-utility build runs.

If you want substrate-native (libelfobj-based) `strip`, see git
history for the now-retired `TASKLIST_STRIP.md`.
