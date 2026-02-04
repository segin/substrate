#!/bin/sh

# Test Here-document Expansion
echo "--- Testing Here-docs ---"
VAR="world"
cat <<EOF
Hello $VAR
Arithmetic: $((1 + 2))
EOF

cat <<'EOF'
Quoted: $VAR
No arithmetic: $((1 + 2))
EOF

# Test Scoping
echo "--- Testing Scoping ---"
GVAR="global"
func() {
    local LVAR="local"
    GVAR="modified_global"
    echo "In func: GVAR=$GVAR, LVAR=$LVAR"
}

echo "Before func: GVAR=$GVAR"
func
echo "After func: GVAR=$GVAR"
echo "LVAR should be empty: LVAR=$LVAR"

# Test Arithmetic in Word Expansion
echo "--- Testing Arithmetic in Word ---"
echo "Calc: $((5 * 5))"
