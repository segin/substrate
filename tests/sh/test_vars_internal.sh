#!/home/segin/test/bin/sh/sh
# tests/sh/test_vars_internal.sh

echo "Testing readonly..."
readonly FOO=bar
FOO=baz  # Should fail
if [ "$FOO" = "bar" ]; then
    echo "[PASS] Readonly FOO preserved"
else
    echo "[FAIL] Readonly FOO changed to $FOO"
fi

unset FOO # Should fail
if [ "$FOO" = "bar" ]; then
    echo "[PASS] Readonly FOO unset prevented"
else
    echo "[FAIL] Readonly FOO unset"
fi

echo "Testing local scoping..."
global_var="global"

func() {
    local local_var="local"
    local global_var="masked"
    echo "inside: local_var=$local_var"
    echo "inside: global_var=$global_var"
}

func
echo "outside: local_var=$local_var"
echo "outside: global_var=$global_var"

if [ "$global_var" = "global" ]; then
    echo "[PASS] Global var restored after masking"
else
    echo "[FAIL] Global var still masked: $global_var"
fi

if [ -z "$local_var" ]; then
    echo "[PASS] Local var not visible outside"
else
    echo "[FAIL] Local var visible outside: $local_var"
fi

echo "Testing export semantics (Shell vs Environment)..."
SHELL_ONLY="i am private"
export EXPORTED="i am public"

# Use env command to check environment of child
if ./bin/sh/sh -c 'echo $SHELL_ONLY' | grep -q "i am private"; then
    echo "[FAIL] SHELL_ONLY leaked to child"
else
    echo "[PASS] SHELL_ONLY not visible to child"
fi

if ./bin/sh/sh -c 'echo $EXPORTED' | grep -q "i am public"; then
    echo "[PASS] EXPORTED visible to child"
else
    echo "[FAIL] EXPORTED not visible to child"
fi
