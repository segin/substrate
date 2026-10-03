include ../../Makefile.inc

# $(O) and $(LIBDIR): see Makefile.inc (i386 in place, x86_64 beside it).
# LIB and SHLIB stay bare names (soname, install link); the files
# themselves are $(O)$(LIB) and $(O)$(SHLIB).
LIB   = libexvi.a
SHLIB = libexvi.so.0
SRCS  = exvi.c exvi_buffer.c exvi_parse.c exvi_runtime.c exvi_cmd.c exvi_session.c exvi_visual.c
OBJS     = $(SRCS:%.c=$(O)%.o)
PIC_OBJS = $(SRCS:%.c=$(O)%.pic.o)
BUILD_MODE := $(if $(NATIVE_BUILD),native,target)
MODE_STAMP = $(O).build-mode

ifeq ($(NATIVE_BUILD),1)
CFLAGS := $(CFLAGS) -idirafter ../../include -idirafter ../../sys/include -idirafter ../../sys
PIC_CFLAGS := $(CFLAGS)
else
CFLAGS := $(CFLAGS) -I../../include -I../../sys/include -I../../sys
PIC_CFLAGS := $(SHLIB_CFLAGS) -I../../include -I../../sys/include -I../../sys
endif

.PHONY: all clean install FORCE

all: $(O)$(LIB) $(if $(NATIVE_BUILD),,$(O)$(SHLIB))

$(O)$(LIB): $(OBJS)
	$(AR) rcs $@ $(OBJS)

# libexvi calls into libregex; declare DT_NEEDED so ld.so loads it.
$(O)$(SHLIB): $(PIC_OBJS)
	$(CC) $(SHLIB_LDFLAGS) -Wl,-soname,$(SHLIB) \
	      -L$(TOP)/usr.lib/regex/$(O) -l:libregex.so.0 \
	      -L$(TOP)/lib/c/$(O) -l:libc.so.0 \
	      -o $@ $(PIC_OBJS)
	# Provide the -lexvi link-time name in the source tree so in-tree consumers
	# (ex, vi) link DT_NEEDED libexvi.so.0 instead of baking in libexvi.a.
	@ln -sf $(SHLIB) $(O)libexvi.so

$(OBJS) $(PIC_OBJS): $(MODE_STAMP)

$(MODE_STAMP): FORCE
	@mkdir -p $(dir $@)
	@mode="$(BUILD_MODE)"; \
	cur=""; \
	if [ -f "$(MODE_STAMP)" ]; then cur=$$(cat "$(MODE_STAMP)"); fi; \
	if [ "$$cur" != "$$mode" ]; then \
		rm -f $(OBJS) $(PIC_OBJS) $(O)$(LIB) $(O)$(SHLIB); \
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
	rm -f $(OBJS) $(PIC_OBJS) $(O)$(LIB) $(O)$(SHLIB) $(O)libexvi.so $(MODE_STAMP)

install: $(O)$(LIB) $(if $(NATIVE_BUILD),,$(O)$(SHLIB))
	@mkdir -p $(DESTDIR)/usr/$(LIBDIR) $(DESTDIR)/$(LIBDIR)
	@mkdir -p $(DESTDIR)/usr/include
	cp $(O)$(LIB) $(DESTDIR)/usr/$(LIBDIR)/
	cp ../../include/exvi.h $(DESTDIR)/usr/include/
ifndef NATIVE_BUILD
	install -m 755 $(O)$(SHLIB) $(DESTDIR)/$(LIBDIR)/
	@ln -sf $(SHLIB) $(DESTDIR)/$(LIBDIR)/libexvi.so
	# Stage the shared object + link symlink under /usr/lib too, so -lexvi
	# resolves to the shared library for the sysroot and on-target builds.
	install -m 755 $(O)$(SHLIB) $(DESTDIR)/usr/$(LIBDIR)/
	@ln -sf $(SHLIB) $(DESTDIR)/usr/$(LIBDIR)/libexvi.so
endif
