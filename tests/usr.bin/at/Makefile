# tests/usr.bin/at/Makefile -- host tests for lib/at.
#
# lib/at/at_exec.c is compiled for the host against mocks of the credential
# calls (see test_at_exec.c).  -idirafter keeps the tree's headers behind
# the host's, so only <at.h> comes from the tree; the substrate syscall
# number SYS_SETSID is mapped to the host's.

CC     ?= cc
CFLAGS  = -Wall -Wextra -g -idirafter ../../../include \
          -include sys/syscall.h -DSYS_SETSID=SYS_setsid

all: test_at_exec

test_at_exec: test_at_exec.c ../../../lib/at/at_exec.c
	$(CC) $(CFLAGS) test_at_exec.c ../../../lib/at/at_exec.c -o $@

check: test_at_exec
	./test_at_exec

clean:
	rm -f test_at_exec

.PHONY: all check clean
