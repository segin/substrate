# Substrate Kernel

This repository contains the Substrate Kernel and associated userland utilities.

## Recent Changes

### /dev/full Implementation

A fully compliant implementation of `/dev/full` has been added.

- **Kernel Driver**: `sys/drivers/devices/full.c`
- **Man Page**: `man/man4/full.4`
- **Regression Tests**: `bin/test_full/`

To test the implementation, run `/bin/test_full/test_full` on the target system.
