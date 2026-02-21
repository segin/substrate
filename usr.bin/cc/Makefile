include ../../Makefile.inc

PROGS = ir-verifier ir-normalize ir-diff
COMMON_SRCS = ir.c parser.c verifier.c serialize.c

ifeq ($(NATIVE_BUILD),1)
    TOOL_CFLAGS = $(CFLAGS)
else
    TOOL_CFLAGS = $(USER_CFLAGS)
endif

all: $(PROGS)

ir-verifier: ir-verifier.c $(COMMON_SRCS) ir.h
	$(CC) $(TOOL_CFLAGS) -o $@ ir-verifier.c $(COMMON_SRCS)

ir-normalize: ir-normalize.c $(COMMON_SRCS) ir.h
	$(CC) $(TOOL_CFLAGS) -o $@ ir-normalize.c $(COMMON_SRCS)

ir-diff: ir-diff.c $(COMMON_SRCS) ir.h
	$(CC) $(TOOL_CFLAGS) -o $@ ir-diff.c $(COMMON_SRCS)

clean:
	rm -f $(PROGS) *.o

install: $(PROGS)
	install -D ir-verifier $(DESTDIR)/usr/bin/ir-verifier
	install -D ir-normalize $(DESTDIR)/usr/bin/ir-normalize
	install -D ir-diff $(DESTDIR)/usr/bin/ir-diff
