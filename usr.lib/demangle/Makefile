include ../../Makefile.inc

# $(O) and $(LIBDIR): see Makefile.inc (i386 in place, x86_64 beside it).
# LIBNAME and SHLIB stay bare names (soname, install link); the files
# themselves are $(O)$(LIBNAME) and $(O)$(SHLIB).
SRCS = buffer.c demangle.c itanium.c rust.c dlang.c
OBJS     = $(SRCS:%.c=$(O)%.o)
PIC_OBJS = $(SRCS:%.c=$(O)%.pic.o)
LIBNAME  = libdemangle.a
SHLIB    = libdemangle.so.0
BUILD_MODE := $(if $(NATIVE_BUILD),native,target)
MODE_STAMP = $(O).build-mode

ifndef NATIVE_BUILD
PIC_CFLAGS := $(SHLIB_CFLAGS) -I../../include -I.
endif

CFLAGS += -I../../include -I.

# Hardened build flags for the host (NATIVE_BUILD=1).  The cross-target
# build is already restricted to -nostdlib / -ffreestanding so most of
# these are inapplicable there; on the host they cost nothing and shrink
# the blast radius if the parser ever does encounter a regression past
# our test coverage.
ifeq ($(BUILD_MODE),native)
CFLAGS += -fstack-protector-strong
CFLAGS += -fstack-clash-protection
CFLAGS += -fno-strict-aliasing
CFLAGS += -Wformat -Wformat-security -Werror=format-security
# _FORTIFY_SOURCE needs an optimisation level; only enable when the
# caller didn't explicitly turn it off (-O0 disables fortify with a
# warning on glibc).
CFLAGS += -D_FORTIFY_SOURCE=2
endif

.PHONY: all clean install FORCE

all: $(O)$(LIBNAME) $(if $(NATIVE_BUILD),,$(O)$(SHLIB))

$(O)$(LIBNAME): $(OBJS)
	$(AR) rcs $@ $(OBJS)

$(O)$(SHLIB): $(PIC_OBJS)
	$(CC) $(SHLIB_LDFLAGS) -Wl,-soname,$(SHLIB) \
	      -L$(TOP)/lib/c/$(O) -l:libc.so.0 \
	      -o $@ $(PIC_OBJS)

$(OBJS) $(PIC_OBJS): $(MODE_STAMP)

$(MODE_STAMP): FORCE
	@mkdir -p $(dir $@)
	@mode="$(BUILD_MODE)"; \
	cur=""; \
	if [ -f "$(MODE_STAMP)" ]; then cur=$$(cat "$(MODE_STAMP)"); fi; \
	if [ "$$cur" != "$$mode" ]; then \
		rm -f $(OBJS) $(PIC_OBJS) $(O)$(LIBNAME) $(O)$(SHLIB); \
		printf '%s\n' "$$mode" > "$(MODE_STAMP).tmp"; \
		mv "$(MODE_STAMP).tmp" "$(MODE_STAMP)"; \
	elif [ ! -f "$(MODE_STAMP)" ]; then \
		printf '%s\n' "$$mode" > "$(MODE_STAMP)"; \
	fi

$(O)%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(O)%.pic.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(PIC_CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(PIC_OBJS) $(O)$(LIBNAME) $(O)$(SHLIB) $(MODE_STAMP)

install: $(O)$(LIBNAME) $(if $(NATIVE_BUILD),,$(O)$(SHLIB))
	@mkdir -p $(DESTDIR)/usr/$(LIBDIR) $(DESTDIR)/$(LIBDIR)
	@mkdir -p $(DESTDIR)/usr/include
	@mkdir -p $(DESTDIR)/usr/man/man3
	cp $(O)$(LIBNAME) $(DESTDIR)/usr/$(LIBDIR)/
	cp ../../include/demangle.h $(DESTDIR)/usr/include/
	cp man3/*.3 $(DESTDIR)/usr/man/man3/
ifndef NATIVE_BUILD
	install -m 755 $(O)$(SHLIB) $(DESTDIR)/$(LIBDIR)/
	@ln -sf $(SHLIB) $(DESTDIR)/$(LIBDIR)/libdemangle.so
endif
