include ../../Makefile.inc

# $(O) and $(LIBDIR): see Makefile.inc (i386 in place, x86_64 beside it).
# LIBNAME and SHLIB stay bare names (soname, install link); the files
# themselves are $(O)$(LIBNAME) and $(O)$(SHLIB).
SRCS     = libjoin.c
OBJS     = $(SRCS:%.c=$(O)%.o)
PIC_OBJS = $(SRCS:%.c=$(O)%.pic.o)
LIBNAME  = libjoin.a
SHLIB    = libjoin.so.0
BUILD_MODE := $(if $(NATIVE_BUILD),native,target)
MODE_STAMP = $(O).build-mode

ifeq ($(NATIVE_BUILD),1)
CFLAGS := $(CFLAGS) -idirafter ../../include -idirafter ../../sys -idirafter ../../sys/include
else
CFLAGS := $(CFLAGS) -I../../include -I../../sys -I../../sys/include
PIC_CFLAGS := $(SHLIB_CFLAGS) -I../../include -I../../sys -I../../sys/include
endif

.PHONY: all clean install FORCE

all: $(O)$(LIBNAME) $(if $(NATIVE_BUILD),,$(O)$(SHLIB))

$(O)$(LIBNAME): $(OBJS)
	$(AR) rcs $@ $(OBJS)

# Versioned soname + DT_NEEDED libc so the resulting .so is a proper
# substrate-target shared object (matches lib/c, lib/m, demangle,
# elfobj, etc. conventions).
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
	cp $(O)$(LIBNAME) $(DESTDIR)/usr/$(LIBDIR)/
	cp ../../include/libjoin.h $(DESTDIR)/usr/include/
ifndef NATIVE_BUILD
	install -m 755 $(O)$(SHLIB) $(DESTDIR)/$(LIBDIR)/
	@ln -sf $(SHLIB) $(DESTDIR)/$(LIBDIR)/libjoin.so
endif
