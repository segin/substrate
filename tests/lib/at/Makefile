include ../../../Makefile.inc

CFLAGS += -I../../../include

all: test_acl fuzz_acl

test_acl: test_acl.o ../../../lib/at/at_acl.c
	$(CC) $(CFLAGS) -DAT_ALLOW_FILE=\"/tmp/test_at.allow\" -DAT_DENY_FILE=\"/tmp/test_at.deny\" -o test_acl test_acl.c ../../../lib/at/at_acl.c

fuzz_acl: fuzz_acl.c ../../../lib/at/at_acl.c
	clang -g -fsanitize=fuzzer,address $(CFLAGS) -DAT_ALLOW_FILE=\"/tmp/fuzz_at.allow\" -DAT_DENY_FILE=\"/tmp/fuzz_at.deny\" -o $@ $^

clean:
	rm -f test_acl fuzz_acl *.o
