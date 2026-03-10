# ELKS Toolchain Bootstrap

`setup_toolchain.sh` installs a pinned `ia16-elf` cross toolchain suitable for
building the ELKS smoke binaries under `tests/elks/`.

## What It Builds

- `binutils-ia16`
- `gcc-ia16`
- bundled GCC prerequisites:
  - `gmp`
  - `mpfr`
  - `mpc`

The script follows the pinned upstream revisions used by the ELKS project
itself in `/home/segin/elks/tools/Makefile`.

## Host Requirements

- POSIX shell
- `make`
- host C compiler toolchain
- `tar`
- `sed`
- either `curl` or `wget`
- development headers/libraries needed by binutils and GCC on the host

## Usage

```sh
tools/elks/setup_toolchain.sh
```

Useful overrides:

```sh
PREFIX="$HOME/opt/substrate-elks" \
WORKDIR="$HOME/build/substrate-elks-toolchain" \
JOBS=8 \
tools/elks/setup_toolchain.sh
```

## Output

The toolchain installs into:

- default: `$HOME/.local/substrate-elks-toolchain`
- override: `$PREFIX`

Add the installed tools to `PATH`:

```sh
export PATH="$HOME/.local/substrate-elks-toolchain/bin:$PATH"
```

## Notes

- The script is intentionally not wired into the main Substrate build.
- It bootstraps only the cross compiler/toolchain needed for `ia16-elf-*`
  binaries; it does not install an ELKS root filesystem.
- Download URLs and version pins can be overridden through the environment:
  - `BINUTILS_VER`
  - `GCC_VER`
  - `GMP_VER`
  - `MPFR_VER`
  - `MPC_VER`
