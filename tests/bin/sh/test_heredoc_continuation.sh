#!/bin/sh
# Regression: a command immediately following a heredoc (no blank
# line in between) used to trip "syntax error near unexpected
# token `<next>'" because read_heredoc cleared the lookahead's
# NEWLINE — which doubles as the command separator.  Verify the
# fix end-to-end with several variants.

echo "--- bare heredoc + command ---"
cat <<X
body line 1
X
echo follow1

echo "--- two heredocs back-to-back ---"
cat <<A
first
A
cat <<B
second
B
echo follow2

echo "--- heredoc inside a pipeline ---"
cat <<C | tr a-z A-Z
piped
C
echo follow3

echo "--- heredoc in a function body ---"
emit() {
    cat <<D
inside-fn
D
    echo follow_inside
}
emit
echo follow4
