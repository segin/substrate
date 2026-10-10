#!/bin/sh
# A branch is the short form wherever the short form reaches: with
# instructions between it and its label and not a directive's filler, and
# where it reaches only because a branch it crosses is itself short.
# See branch-relax-check.sh.
#
# Going long a few bytes early is not wrong code, but it is not the code
# GNU as writes, and every such branch is three or four bytes more.
here=$(dirname "$0")
rc=0
sh "$here/branch-relax-check.sh" fill .Lt tight || rc=1
sh "$here/branch-relax-check.sh" nop .Lt one grew tight || rc=1
exit "$rc"
