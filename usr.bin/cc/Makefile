include ../../Makefile.inc

PROGS = cc ir-verifier ir-normalize ir-diff
COMMON_SRCS = ir.c parser.c verifier.c serialize.c
CC_SRCS = cmd/cc.c cmd/pipeline.c \
	  frontend/lexer.c frontend/parser.c frontend/sema.c frontend/builtin.c \
	  middle/ast2ir.c middle/legalize.c \
	  middle/ssa/module.c middle/ssa/func.c middle/ssa/bblock.c middle/ssa/instr_set.c \
	  backend/select.c backend/regalloc.c backend/frame.c backend/emit_s.c

ifeq ($(NATIVE_BUILD),1)
    TOOL_CFLAGS = $(CFLAGS)
else
    TOOL_CFLAGS = $(USER_CFLAGS)
endif

all: $(PROGS)

cc: $(CC_SRCS) include/cc_driver.h include/cc_frontend.h include/cc_ssa.h include/cc_backend.h include/cc_pipeline.h
	$(CC) $(TOOL_CFLAGS) -Iinclude -o $@ $(CC_SRCS)

ir-verifier: ir-verifier.c $(COMMON_SRCS) ir.h
	$(CC) $(TOOL_CFLAGS) -o $@ ir-verifier.c $(COMMON_SRCS)

ir-normalize: ir-normalize.c $(COMMON_SRCS) ir.h
	$(CC) $(TOOL_CFLAGS) -o $@ ir-normalize.c $(COMMON_SRCS)

ir-diff: ir-diff.c $(COMMON_SRCS) ir.h
	$(CC) $(TOOL_CFLAGS) -o $@ ir-diff.c $(COMMON_SRCS)

clean:
	rm -f $(PROGS) *.o

install: $(PROGS)
	install -D cc $(DESTDIR)/usr/bin/cc
	install -D ir-verifier $(DESTDIR)/usr/bin/ir-verifier
	install -D ir-normalize $(DESTDIR)/usr/bin/ir-normalize
	install -D ir-diff $(DESTDIR)/usr/bin/ir-diff
