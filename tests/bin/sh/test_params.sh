# Test Length
export x=hello
echo Len: ${#x}
# Test Suffix
export file=foo.c
echo Suffix: ${file%.c}
# Test Unset Error (run in subshell to avoid killing script)
( unset y; echo ${y?ShouldError} )
# Test null error
( export z=""; echo ${z:?NullError} )
# Test Existing Prefix
echo Prefix: ${file#foo}
# Test Existing Default
unset w
echo Default: ${w:-def}
