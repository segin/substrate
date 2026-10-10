DYNAMIC = 1

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
    # Dynamic-link the cc/cpp/ir-* tools (mirrors Makefile.bin.inc
    # DYNAMIC=1 — this Makefile rolls its own toolchain glue
    # because it builds five PROGS with shared object sets).
    TOOL_CFLAGS = $(filter-out -fno-pie,$(USER_CFLAGS)) -fPIE \
                  -fno-builtin -DCC_SUBSTRATE_BUILD=1
    TOOL_LDFLAGS = -m32 -nostdlib -pie -fPIE \
                   -Wl,--dynamic-linker=/sbin/ld.so \
                   -Wl,--unresolved-symbols=ignore-in-shared-libs
    TOOL_CRT0 = $(TOP)/lib/c/crt0.o
    # libc.so.0 declares DT_NEEDED libm.so.0 + DT_NEEDED libsys.so.0,
    # so the only thing we need on the link line is libc itself.
    TOOL_LIBC = $(TOP)/lib/c/libc.so.0
    TOOL_LIBS = -L$(TOP)/lib/c -l:libc.so.0 $(LIBGCC)
    TOOL_BRAND = $(call BRAND_SUBSTRATE,$@)
endif

.PHONY: all clean install FORCE

# ARCH=x86_64 builds the compiler driver alone, as a native 64-bit program,
# into $(O) (obj-x86_64/) beside the 32-bit build, the way Makefile.bin.inc
# does for the programs that use it: a PIE loaded by /sbin/ld64.so against
# the 64-bit libc.  The ir-* tools are development aids and stay 32-bit.
ifeq ($(ARCH)$(NATIVE_BUILD),x86_64)
CC64_OBJS = $(CC_SRCS:%.c=$(O)%.o)
CC64_CRT0 = $(TOP)/lib/c/$(O)crt0.o
CC64_LIBC = $(TOP)/lib/c/$(O)libc.so.0
CC64_LDFLAGS = -m64 -nostdlib -pie -fPIE \
               -Wl,--dynamic-linker=/sbin/ld64.so \
               -Wl,--unresolved-symbols=ignore-in-shared-libs \
               -Wl,-z,max-page-size=0x1000
CC64_LIBS = -L$(TOP)/lib/c/$(O) -l:libc.so.0 \
            -L$(TOP)/lib/sys/$(O) -l:libsys.so.0 $(LIBGCC)

all: $(O)cc

$(CC64_CRT0) $(CC64_LIBC): FORCE
	$(MAKE) -C $(TOP)/lib/c

$(O)cc: $(CC64_OBJS) $(CC64_LIBC) $(CC64_CRT0)
	$(CC) $(TOOL_CFLAGS) $(CC64_LDFLAGS) -o $@ $(CC64_OBJS) $(CC64_CRT0) $(CC64_LIBS)
	$(call BRAND_SUBSTRATE,$@)

$(CC64_OBJS): $(CC_HEADERS)

$(O)%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(TOOL_CFLAGS) $(TOOL_CPPFLAGS) -c $< -o $@
else
all: $(PROGS)
endif

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
	rm -rf obj-x86_64

install: $(PROGS)
	install -D cc $(DESTDIR)/usr/bin/cc
	ln -sf cc $(DESTDIR)/usr/bin/cpp
	install -D scc $(DESTDIR)/usr/bin/scc
	install -D ir-verifier $(DESTDIR)/usr/bin/ir-verifier
	install -D ir-normalize $(DESTDIR)/usr/bin/ir-normalize
	install -D ir-diff $(DESTDIR)/usr/bin/ir-diff
	install -d $(DESTDIR)/usr/lib/substratecc/include
	install -m 644 resource/include/*.h $(DESTDIR)/usr/lib/substratecc/include/
