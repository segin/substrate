# texinfo on Substrate

GNU Texinfo 7.3: the `info` reader, `install-info`, and the `texi2any`
(`makeinfo`) documentation translator.

## Dependencies

- Build: ncurses (for `info`) staged in the cross sysroot, and a host perl,
  which configure uses and the build runs to generate sources.
- Runtime: perl.  `texi2any`, `makeinfo`, `pod2texi` and `texi2dvi` are perl
  and shell scripts whose `#!` line names `/usr/bin/perl`, so `contrib/perl`
  precedes this port.  `info` and `install-info` are C and need nothing else.

## No patches

The port carries no patch series.

## Build notes

- Configured `--host=i386-unknown-linux-gnu` with the substrate cross gcc as
  `CC`.  `build.sh` stamps `ELFOSABI_SUBSTRATE` (0x40) into the ELF programs
  only; the rest of `/usr/bin` is scripts.
- `--disable-perl-xs`: texi2any can load optional XS modules for speed.
  Building them means compiling against the target perl's headers and
  configuration, which a cross build does not have.  Without them texi2any
  uses its pure-perl implementation, which produces the same output.
- `--disable-nls`: no translations are installed.
- `/usr/share/info/dir` is not shipped; the directory file is assembled on
  the image from every package's pages.
