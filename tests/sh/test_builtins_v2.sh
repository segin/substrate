#!/home/segin/test/bin/sh/sh
# tests/sh/test_builtins_v2.sh

echo "Diagnostics:"
command -v false || echo "false not found in path"
echo "PATH=$PATH"

echo "Testing CDPATH..."
rm -rf /tmp/cdpath_test
mkdir -p /tmp/cdpath_test/sub_dir
export CDPATH="/tmp/cdpath_test"
echo "Attempting: cd sub_dir"
cd sub_dir
ERR=$?
CWD=$(pwd)
echo "Result code: $ERR, CWD: $CWD"

if [ "$CWD" = "/tmp/cdpath_test/sub_dir" ]; then
    echo "[PASS] cd sub_dir found via CDPATH"
else
    echo "[FAIL] cd sub_dir failed via CDPATH"
fi
cd /

echo "Testing eval status..."
echo "Running: eval true"
eval "true"
S1=$?
echo "eval true status: $S1"

echo "Running: eval false"
# If false is missing, this should be 127. If it exists, should be 1.
eval "false"
S2=$?
echo "eval false status: $S2"

if [ $S2 -ne 0 ]; then
    echo "[PASS] eval false returns non-zero ($S2)"
else
    echo "[FAIL] eval false returned 0"
fi
