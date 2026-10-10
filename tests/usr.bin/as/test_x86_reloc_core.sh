#!/bin/sh
# The x86 relocation table: each kind's number is the ABI's, a kind is
# refused for the other machine, and every relocation written is found by
# readelf under its own name.  See reloc-table-check.sh.
exec sh "$(dirname "$0")/reloc-table-check.sh" x86 i386 x86_64
