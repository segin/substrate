include ../../Makefile.inc

PROGS = cc cpp ir-verifier ir-normalize ir-diff
COMMON_SRCS = ir.c parser.c verifier.c serialize.c
COMMON_OBJS = $(COMMON_SRCS:.c=.o)
CC_SRCS = cmd/cc.c cmd/pipeline.c \
	  frontend/preproc.c frontend/lexer.c frontend/parser.c frontend/sema.c frontend/builtin.c \
	  middle/ast2ir.c middle/legalize.c \
	  middle/passes/opt.c \
	  middle/ssa/module.c middle/ssa/func.c middle/ssa/bblock.c middle/ssa/instr_set.c \
	  backend/select.c backend/regalloc.c backend/frame.c backend/emit_s.c
CC_OBJS = $(CC_SRCS:.c=.o)
IR_VERIFIER_OBJS = ir-verifier.o $(COMMON_OBJS)
IR_NORMALIZE_OBJS = ir-normalize.o $(COMMON_OBJS)
IR_DIFF_OBJS = ir-diff.o $(COMMON_OBJS)
ALL_OBJS = $(CC_OBJS) $(COMMON_OBJS) ir-verifier.o ir-normalize.o ir-diff.o
TOOL_CPPFLAGS = -Iinclude
BUILD_MODE := $(if $(NATIVE_BUILD),native,target)
MODE_STAMP = .build-mode
CC_HEADERS = include/cc_driver.h include/cc_frontend.h include/cc_ssa.h \
	     include/cc_backend.h include/cc_mir.h include/cc_pipeline.h \
	     include/cc_middle.h include/cc_target.h ir.h

ifeq ($(NATIVE_BUILD),1)
    TOOL_CFLAGS = $(CFLAGS) -DCC_NATIVE_BUILD=1
    TOOL_LDFLAGS =
    TOOL_CRT0 =
    TOOL_LIBS =
    TOOL_BRAND = @true
else
    TOOL_CFLAGS = $(USER_CFLAGS) -fno-builtin -DCC_SUBSTRATE_BUILD=1
    TOOL_LDFLAGS = $(USER_LDFLAGS)
    TOOL_CRT0 = $(TOP)/lib/c/crt0.o
    TOOL_LIBC = $(TOP)/lib/c/libc.a
    # libc.a's fpclassify.o references feraiseexcept (in libm),
    # so static-link binaries need libm too.
    TOOL_LIBS = -L$(TOP)/lib/c -lc -L$(TOP)/lib/m -lm $(LIBGCC)
    TOOL_BRAND = $(call BRAND_SUBSTRATE,$@)
endif

.PHONY: all clean install FORCE

all: $(PROGS)

$(ALL_OBJS): $(MODE_STAMP)

$(MODE_STAMP): FORCE
	@mode="$(BUILD_MODE)"; \
	cur=""; \
	if [ -f "$(MODE_STAMP)" ]; then cur=$$(cat "$(MODE_STAMP)"); fi; \
	if [ "$$cur" != "$$mode" ]; then \
		rm -f $(ALL_OBJS) $(PROGS); \
		printf '%s\n' "$$mode" > "$(MODE_STAMP).tmp"; \
		mv "$(MODE_STAMP).tmp" "$(MODE_STAMP)"; \
	elif [ ! -f "$(MODE_STAMP)" ]; then \
		printf '%s\n' "$$mode" > "$(MODE_STAMP)"; \
	fi

ifndef NATIVE_BUILD
$(TOOL_CRT0) $(TOOL_LIBC): FORCE
	$(MAKE) -C $(TOP)/lib/c
endif

cc: $(CC_OBJS) $(TOOL_LIBC) $(TOOL_CRT0)
	$(CC) $(TOOL_CFLAGS) $(TOOL_LDFLAGS) -o $@ $(CC_OBJS) $(TOOL_CRT0) $(TOOL_LIBS)
	$(TOOL_BRAND)

cpp: cc
	ln -sf cc cpp

ir-verifier: $(IR_VERIFIER_OBJS) $(TOOL_LIBC) $(TOOL_CRT0)
	$(CC) $(TOOL_CFLAGS) $(TOOL_LDFLAGS) -o $@ $(IR_VERIFIER_OBJS) $(TOOL_CRT0) $(TOOL_LIBS)
	$(TOOL_BRAND)

ir-normalize: $(IR_NORMALIZE_OBJS) $(TOOL_LIBC) $(TOOL_CRT0)
	$(CC) $(TOOL_CFLAGS) $(TOOL_LDFLAGS) -o $@ $(IR_NORMALIZE_OBJS) $(TOOL_CRT0) $(TOOL_LIBS)
	$(TOOL_BRAND)

ir-diff: $(IR_DIFF_OBJS) $(TOOL_LIBC) $(TOOL_CRT0)
	$(CC) $(TOOL_CFLAGS) $(TOOL_LDFLAGS) -o $@ $(IR_DIFF_OBJS) $(TOOL_CRT0) $(TOOL_LIBS)
	$(TOOL_BRAND)

$(CC_OBJS): $(CC_HEADERS)
$(COMMON_OBJS): ir.h
ir-verifier.o ir-normalize.o ir-diff.o: ir.h

%.o: %.c
	$(CC) $(TOOL_CFLAGS) $(TOOL_CPPFLAGS) -c $< -o $@

clean:
	rm -f $(PROGS) $(ALL_OBJS) $(MODE_STAMP)

install: $(PROGS)
	install -D cc $(DESTDIR)/usr/bin/cc
	ln -sf cc $(DESTDIR)/usr/bin/cpp
	install -D scc $(DESTDIR)/usr/bin/scc
	install -D ir-verifier $(DESTDIR)/usr/bin/ir-verifier
	install -D ir-normalize $(DESTDIR)/usr/bin/ir-normalize
	install -D ir-diff $(DESTDIR)/usr/bin/ir-diff
	install -d $(DESTDIR)/usr/lib/substratecc/include
	install -m 644 resource/include/*.h $(DESTDIR)/usr/lib/substratecc/include/
