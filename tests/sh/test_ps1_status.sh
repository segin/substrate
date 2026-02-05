#!/bin/sh

# Test that PS1 expansion doesn't clobber $?

# We need to run this in interactive mode (-i) to trigger prompt evaluation.
# We'll rely on the shell outputting the prompt to stderr (or stdout depending on implementation).
# Our shell prints prompt to printf/stdout.

# Case 1: PS1 with command sub that exits with different status
cat > test_ps1.sh << 'EOF'
PS1='$(exit 42)> '
# Set status to 0
true
# The prompt will be evaluated here before reading next line.
# If prompt expansion clobbers $?, next line sees 42.
# If preserved, it sees 0.
echo "STATUS=$?"
EOF

# Run with -i. Input from test_ps1.sh? 
# If we redirect input, is_interactive logic in sh.c:
# int is_interactive = ((input == stdin && isatty(STDIN_FILENO)) || opt_i) && !opt_c;
# So we need -i.

./bin/sh/sh -i < test_ps1.sh > output.log 2>&1

if grep -q "STATUS=0" output.log; then
    echo "PASS: Status preserved"
else
    echo "FAIL: Status clobbered"
    cat output.log
    exit 1
fi

rm test_ps1.sh output.log
