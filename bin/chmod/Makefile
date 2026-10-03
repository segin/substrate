PROG = chmod
SRCS = chmod.c mode_parser.c

USER_CFLAGS += -iquote ../../include

ifdef NATIVE_BUILD
USER_CFLAGS += -DNATIVE_BUILD=1
endif

DYNAMIC = 1

include ../../Makefile.inc
include ../../Makefile.bin.inc

# $(O): see Makefile.inc (i386 in place, x86_64 under obj-x86_64/).
OBJS += $(O)modeparse.o
EXTRA_CLEAN += $(O)modeparse.o

$(O)$(PROG): $(O)modeparse.o

$(O)modeparse.o: ../../lib/modeparse.c ../../include/modeparse.h $(BUILD_RULE_DEPS)
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) -c ../../lib/modeparse.c -o $@
