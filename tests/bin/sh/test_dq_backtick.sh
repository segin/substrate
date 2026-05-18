#!/bin/sh
# Regression: \` inside double quotes used to be stripped by the
# lexer, leaving a bare ` that expand_word then interpreted as
# command substitution.  GNU-doc-style "`foo'" quoting (`echo
# "in \`${var}'..."`) tripped this constantly in old configure
# scripts and elsewhere.

echo "--- escaped backtick in dq ---"
PWD=somewhere
echo "see \`${PWD}' for details"

echo "--- two escaped backticks bracketing arg ---"
echo "before \`payload\` after"

echo "--- escaped dollar in dq ---"
VAR=value
echo "literal \$VAR and expanded $VAR"

echo "--- escaped double-quote in dq ---"
echo "a \"quoted\" word"

echo "--- escaped backslash in dq ---"
echo "back\\slash"
