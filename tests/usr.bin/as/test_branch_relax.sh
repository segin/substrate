#!/bin/sh
# A branch to a compiler's label is short where the label is in reach and
# long where it is not, at every distance about the limit, forward and
# back; and a branch across one that had to be long counts it as long.
# See branch-relax-check.sh.
#
# The same held with instructions for filler, and where a branch is short
# only by another's true size, is test_branch_relax_exact.sh; to ordinary
# labels, test_branch_relax_label.sh.
exec sh "$(dirname "$0")/branch-relax-check.sh" fill .Lt one grew
