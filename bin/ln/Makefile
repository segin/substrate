PROG = ln
SRCS = ln.c ln_options.c ln_path.c ln_backup.c ln_execute.c

include ../../Makefile.inc

USER_CFLAGS += -DLINK_HAVE_SYMLINK=1

ifeq ($(NATIVE_BUILD),1)
USER_CFLAGS += -idirafter ../../include
else
USER_CFLAGS += -I../../include
endif

include ../../Makefile.bin.inc

TEST_CFLAGS = -O1 -g -Wall -Wextra -D_GNU_SOURCE -DLINK_HAVE_SYMLINK=1 -idirafter ../../include
EXTRA_CLEAN += ln_host

.PHONY: test integration

ln_host: $(SRCS) ln.h
	$(CC) $(TEST_CFLAGS) -o $@ $(SRCS)

TESTS = ../../tests/bin/ln

integration: ln_host
	sh $(TESTS)/test_integration.sh ./ln_host

test: integration
