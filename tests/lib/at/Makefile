include ../../../Makefile.inc

CFLAGS += -I../../../include

# Cancel make's built-in yacc/lex rules: ../../../lib/at/at_parse.c is
# hand-written and shares its stem with at_parse.y, so naming it as a
# prerequisite here let make "update" it by running yacc over it and
# clobbering the real source -- lib/at/Makefile carries the same guard.
%.c: %.y
%.c: %.l

all: test_acl test_parse fuzz_acl

test_parse: test_parse.c ../../../lib/at/at_parse.c
	$(CC) $(CFLAGS) -o $@ $^

test_acl: test_acl.o ../../../lib/at/at_acl.c
	$(CC) $(CFLAGS) -DAT_ALLOW_FILE=\"/tmp/test_at.allow\" -DAT_DENY_FILE=\"/tmp/test_at.deny\" -o test_acl test_acl.c ../../../lib/at/at_acl.c

fuzz_acl: fuzz_acl.c ../../../lib/at/at_acl.c
	clang -g -fsanitize=fuzzer,address $(CFLAGS) -DAT_ALLOW_FILE=\"/tmp/fuzz_at.allow\" -DAT_DENY_FILE=\"/tmp/fuzz_at.deny\" -o $@ $^

clean:
	rm -f test_acl test_parse fuzz_acl *.o
