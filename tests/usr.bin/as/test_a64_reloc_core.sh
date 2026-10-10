#!/bin/sh
# The AArch64 relocation table: each kind's number is the ABI's, a kind
# is refused for another machine, and every relocation written is found
# by readelf under its own name.  See reloc-table-check.sh.
exec sh "$(dirname "$0")/reloc-table-check.sh" a64 aarch64
