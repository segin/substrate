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

# BIN_CFLAGS, not USER_CFLAGS: this object goes into the same link as the
# ones Makefile.bin.inc compiles, so it has to be built the same way.
# DYNAMIC = 1 makes that link a PIE, and USER_CFLAGS says -fno-pie.
$(O)modeparse.o: ../../lib/modeparse.c ../../include/modeparse.h $(BUILD_RULE_DEPS)
	@mkdir -p $(dir $@)
	$(CC) $(BIN_CFLAGS) -c ../../lib/modeparse.c -o $@
