# Test Assignments
TARGET=foo sh -c 'echo $TARGET' # Should print foo
echo "Old: $TARGET" # Should be empty/old value

VAR=val
echo $VAR # val

# Test Case
x=foo
case $x in bar) echo no ;; foo) echo yes ;; *) echo default ;; esac

# Test Case Default
y=baz
case $y in a) echo no ;; *) echo default ;; esac

# Test Pattern Matching in Case
f=foo.c
case $f in *.c) echo C File ;; *) echo Unknown ;; esac
