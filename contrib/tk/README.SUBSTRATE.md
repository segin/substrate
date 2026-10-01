# Tk 8.6.16 — substrate port

Tcl's GUI toolkit for X11: `wish`, `libtk8.6.a`, the Tk and Ttk script
library, headers and `tkConfig.sh`.  Version-locked to `contrib/tcl`.

## Build

```sh
./fetch.sh        # SHA-256 matches Arch's tk 8.6.16-1
./build.sh        # -> dist-tk/usr/{bin/wish,lib/libtk8.6.a,lib/tk8.6/,include/tk.h,...}
```

Prerequisites, all staged first: **contrib/tcl** (its `build/` tree, see
below), the X client stack (`libX11`, `libXext`, `libxcb`, `libXau`,
`libXdmcp`), **libXft**, **libXrender**, **libXScrnSaver**, **freetype** and
**fontconfig**.

## Substrate notes

- **Configured against Tcl's build tree.**  `--with-tcl` points at
  `contrib/tcl/build/tcl8.6.16/unix`, not the staged `tclConfig.sh`: the
  staged one describes the target's `/usr` (`TCL_LIB_SPEC='-L/usr/lib ...'`),
  which on the build host names the host's own libraries.  From the build
  tree, Tk's configure uses `TCL_BUILD_LIB_SPEC` and `TCL_SRC_DIR`.
- **Static.**  `--disable-shared`, like Tcl: there is no shared `libtcl` for
  a shared `libtk` to load against.  `wish` links both archives and the X
  libraries dynamically.  `package require Tk` from `tclsh` therefore does
  not work; use `wish`.
- **Fonts through Xft.**  The image has no TrueType fonts, but fontconfig
  serves the X bitmap fonts (`font-adobe-75dpi`/`100dpi`, `font-misc-misc`)
  to Xft, so Tk sees Helvetica, Times, Courier, the Lucida families, New
  Century Schoolbook and Fixed.  `TkDefaultFont` resolves to Helvetica 12.
- **XScreenSaver** (`libXss`) backs `tk inactive`.
- No patches: the tree configures and builds as-is, with no warnings.

## Tested

Under Xfbdev (1024x768x32): label, entry, button, checkbutton,
`ttk::button`, `ttk::progressbar` and a canvas with filled shapes and text
all render; `tk windowingsystem` is `x11`.
