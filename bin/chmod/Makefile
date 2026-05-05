PROG = chmod
SRCS = chmod.c mode_parser.c

USER_CFLAGS += -iquote ../../include

ifdef NATIVE_BUILD
USER_CFLAGS += -DNATIVE_BUILD=1
endif

include ../../Makefile.inc
include ../../Makefile.bin.inc

OBJS += modeparse.o
EXTRA_CLEAN += modeparse.o

$(PROG): modeparse.o

modeparse.o: ../../lib/modeparse.c ../../include/modeparse.h $(BUILD_RULE_DEPS)
	$(CC) $(USER_CFLAGS) -c ../../lib/modeparse.c -o $@
