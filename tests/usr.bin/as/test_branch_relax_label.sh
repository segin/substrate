#!/bin/sh
# A branch to an ordinary label of its own section -- not one of a
# compiler's .L labels -- is resolved like any other: sized by its
# distance, and with no relocation left for the linker.
# See branch-relax-check.sh.
exec sh "$(dirname "$0")/branch-relax-check.sh" fill target one grew tight
