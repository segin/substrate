include ../../Makefile.inc

# $(O) and $(LIBDIR): see Makefile.inc (i386 in place, x86_64 beside it).
# LIBNAME and SHLIB stay bare names (soname, install link); the files
# themselves are $(O)$(LIBNAME) and $(O)$(SHLIB).
SRCS     = readproc.c
OBJS     = $(SRCS:%.c=$(O)%.o)
PIC_OBJS = $(SRCS:%.c=$(O)%.pic.o)
LIBNAME  = libproc.a
SHLIB    = libproc.so.0
BUILD_MODE := $(if $(NATIVE_BUILD),native,target)
MODE_STAMP = $(O).build-mode

ifndef NATIVE_BUILD
PIC_CFLAGS := $(SHLIB_CFLAGS) -I../../include
endif
CFLAGS += -I../../include

.PHONY: all clean install FORCE

all: $(O)$(LIBNAME) $(if $(NATIVE_BUILD),,$(O)$(SHLIB))

$(O)$(LIBNAME): $(OBJS)
	$(AR) rcs $@ $(OBJS)

$(O)$(SHLIB): $(PIC_OBJS)
	$(CC) $(SHLIB_LDFLAGS) -Wl,-soname,$(SHLIB) \
	      -L$(TOP)/lib/sys/$(O) -l:libsys.so.0 \
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
	@mkdir -p $(DESTDIR)/usr/include/proc
	cp $(O)$(LIBNAME) $(DESTDIR)/usr/$(LIBDIR)/
	cp ../../include/proc/readproc.h $(DESTDIR)/usr/include/proc/
ifndef NATIVE_BUILD
	install -m 755 $(O)$(SHLIB) $(DESTDIR)/$(LIBDIR)/
	@ln -sf $(SHLIB) $(DESTDIR)/$(LIBDIR)/libproc.so
endif
