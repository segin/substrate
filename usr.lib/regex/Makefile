include ../../Makefile.inc

CFLAGS := $(CFLAGS) -I../../include -I../../sys/include
LDFLAGS := $(USER_LDFLAGS)

ifndef NATIVE_BUILD
PIC_CFLAGS := $(SHLIB_CFLAGS) -I../../include -I../../sys/include
endif

SRCS = src/compile.c src/exec.c src/util.c src/engine_posix.c src/engine_safe.c \
       src/engine_pcre2_adapter.c src/posix_compat.c

# $(O) and $(LIBDIR): see Makefile.inc (i386 in place, x86_64 beside it).
# LIBNAME and SHLIB stay bare names (soname, install link); the files
# themselves are $(O)$(LIBNAME) and $(O)$(SHLIB).
OBJS     = $(SRCS:%.c=$(O)%.o)
PIC_OBJS = $(SRCS:%.c=$(O)%.pic.o)

LIBNAME = libregex.a
SHLIB   = libregex.so.0
BUILD_MODE := $(if $(NATIVE_BUILD),native,target)
MODE_STAMP = $(O).build-mode

# Optional integrations
# USE_PCRE2=1 enables PCRE2 adapter (requires pcre2 headers/libs on host)
# USE_ICU=1 enables ICU-based unicode casefolding in the safe engine

ifeq ($(USE_PCRE2),1)
CFLAGS += -DREGEX_USE_PCRE2
endif

ifeq ($(USE_ICU),1)
CFLAGS += -DREGEX_USE_ICU
endif

.PHONY: all clean install FORCE

all: $(O)$(LIBNAME) $(if $(NATIVE_BUILD),,$(O)$(SHLIB))

$(O)$(LIBNAME): $(OBJS)
	$(AR) rcs $@ $(OBJS)

$(O)$(SHLIB): $(PIC_OBJS)
	$(CC) $(SHLIB_LDFLAGS) -Wl,-soname,$(SHLIB) \
	      -L$(TOP)/lib/c/$(O) -l:libc.so.0 \
	      -o $@ $(PIC_OBJS)
	# libregex is a shared dependency: provide the -lregex link-time name in the
	# source tree so in-tree consumers (sed, ex, vi, find, diff, exvi, ...) link
	# DT_NEEDED libregex.so.0 rather than baking in libregex.a.  (grep already
	# links -l:libregex.so.0 explicitly.)
	@ln -sf $(SHLIB) $(O)libregex.so

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
	rm -f $(OBJS) $(PIC_OBJS) $(O)$(LIBNAME) $(O)$(SHLIB) $(O)libregex.so $(MODE_STAMP)

install: $(O)$(LIBNAME) $(if $(NATIVE_BUILD),,$(O)$(SHLIB))
	@mkdir -p $(DESTDIR)/usr/$(LIBDIR) $(DESTDIR)/$(LIBDIR)
	@mkdir -p $(DESTDIR)/usr/include/regex
	@mkdir -p $(DESTDIR)/usr/$(LIBDIR)/pkgconfig
	@mkdir -p $(DESTDIR)/usr/man/man3
	cp $(O)$(LIBNAME) $(DESTDIR)/usr/$(LIBDIR)/
	cp ../../include/regex.h $(DESTDIR)/usr/include/
	cp ../../include/regex/flags.h $(DESTDIR)/usr/include/regex/
	cp regex.pc $(DESTDIR)/usr/$(LIBDIR)/pkgconfig/
	if [ -f man3/regex.3 ]; then cp man3/regex.3 $(DESTDIR)/usr/man/man3/; fi
ifndef NATIVE_BUILD
	install -m 755 $(O)$(SHLIB) $(DESTDIR)/$(LIBDIR)/
	@ln -sf $(SHLIB) $(DESTDIR)/$(LIBDIR)/libregex.so
	# Also stage the shared object + its link-time symlink under /usr/lib so
	# -lregex resolves to the shared library (DT_NEEDED libregex.so.0) for both
	# the cross sysroot and on-target compilation, making libregex a real shared
	# dependency instead of a static archive baked into every consumer.
	install -m 755 $(O)$(SHLIB) $(DESTDIR)/usr/$(LIBDIR)/
	@ln -sf $(SHLIB) $(DESTDIR)/usr/$(LIBDIR)/libregex.so
endif
