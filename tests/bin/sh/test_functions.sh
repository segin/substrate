
# Basic Function
greet() { echo Hello $1; }
greet World

# Function Scoping (Args)
scope() { echo Inside: Arg1=$1; }
set -- Global Arg 1
echo Outside Before: Arg1=$1
scope Local
echo Outside After: Arg1=$1

# Return Status
ret_test() { return 42; echo "Should not print"; }
ret_test
echo Return: $?

# Variable Scope (Global)
var_scope() { GLOBAL=changed; }
GLOBAL=initial
var_scope
echo GLOBAL=$GLOBAL

# Recursive (Simple stub)
recurse() { echo Recurse $1; }
recurse 1
