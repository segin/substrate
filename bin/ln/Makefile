PROG = ln
SRCS = ln.c

include ../../Makefile.inc

ifeq ($(NATIVE_BUILD),1)
USER_CFLAGS += -idirafter ../../include
else
USER_CFLAGS += -I../../include
endif

LDADD += -L../../usr.lib/link -llink

.PHONY: linklib

linklib:
	$(MAKE) -C ../../usr.lib/link clean all NATIVE_BUILD=$(NATIVE_BUILD)

$(PROG): linklib

include ../../Makefile.bin.inc

TEST_CFLAGS = -O1 -g -Wall -Wextra -D_GNU_SOURCE -DLINK_HAVE_SYMLINK=1 -idirafter ../../include
EXTRA_CLEAN += ln_host

.PHONY: test integration

ln_host: ln.c ../../usr.lib/link/liblink.c ../../include/liblink.h
	$(CC) $(TEST_CFLAGS) -o $@ ln.c ../../usr.lib/link/liblink.c

integration: ln_host
	sh tests/test_integration.sh ./ln_host

test: integration
