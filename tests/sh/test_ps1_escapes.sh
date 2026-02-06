#!/bin/sh

# Test PS1 escape sequences

# Case 1: \w (CWD)
cat > test_ps1_cwd.sh << 'EOF'
PS1='\w> '
true
EOF

CWD=$(pwd)
./bin/sh/sh -i < test_ps1_cwd.sh > output.log 2>&1
if grep -q "$CWD> " output.log; then
    echo "PASS: \w expands to CWD"
else
    echo "FAIL: \w failed"
    cat output.log
    exit 1
fi

# Case 2: \u (User) - NATIVE_BUILD, so likely current user or fallback
# Since we can't easily predict username in all envs without 'whoami',
# we'll test that it expands to SOMETHING non-literal.
cat > test_ps1_user.sh << 'EOF'
PS1='USER:\u> '
true
EOF

./bin/sh/sh -i < test_ps1_user.sh > output.log 2>&1
if grep -q "USER:" output.log && ! grep -q "USER:\\u" output.log; then
    echo "PASS: \u expanded"
else
    echo "FAIL: \u failed"
    cat output.log
    exit 1
fi

rm test_ps1_cwd.sh test_ps1_user.sh output.log
