PROG = mv
SRCS = mv.c mv_opts.c mv_path.c mv_prompt.c mv_backup.c mv_rename.c
DYNAMIC = 1

include ../../Makefile.inc
include ../../Makefile.bin.inc

# ----------------------------------------------------------------
# host-side test infrastructure (mirrors bin/rm/Makefile)
# ----------------------------------------------------------------
TEST_CFLAGS = -O1 -g -Wall -Wextra -D_GNU_SOURCE
TESTS       = ../../tests/bin/mv

EXTRA_CLEAN += mv_host

.PHONY: test unit integration

mv_host: $(SRCS) mv.h mv_path.h mv_prompt.h mv_backup.h
	$(CC) $(TEST_CFLAGS) -o $@ $(SRCS)

integration: mv_host
	sh $(TESTS)/test_integration.sh ./mv_host

unit:
	@echo "mv has no separate unit-test binaries yet; integration covers parser + engine"

test: unit integration
