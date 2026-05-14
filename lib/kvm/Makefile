include ../../Makefile.inc

SRCS     = kvm.c
OBJS     = $(SRCS:.c=.o)
PIC_OBJS = $(SRCS:.c=.pic.o)
LIBNAME  = libkvm.a
SHLIB    = libkvm.so.0
BUILD_MODE := $(if $(NATIVE_BUILD),native,target)
MODE_STAMP = .build-mode

ifndef NATIVE_BUILD
PIC_CFLAGS := $(SHLIB_CFLAGS) -I../../include
endif
CFLAGS += -I../../include

.PHONY: all clean install FORCE

all: $(LIBNAME) $(if $(NATIVE_BUILD),,$(SHLIB))

$(LIBNAME): $(OBJS)
	$(AR) rcs $@ $(OBJS)

# libkvm wraps libsys; declare DT_NEEDED so ld.so loads it.
$(SHLIB): $(PIC_OBJS)
	$(CC) $(SHLIB_LDFLAGS) -Wl,-soname,$(SHLIB) \
	      -L$(TOP)/lib/sys -l:libsys.so.0 \
	      -L$(TOP)/lib/c -l:libc.so.0 \
	      -o $@ $(PIC_OBJS)

$(OBJS) $(PIC_OBJS): $(MODE_STAMP)

$(MODE_STAMP): FORCE
	@mode="$(BUILD_MODE)"; \
	cur=""; \
	if [ -f "$(MODE_STAMP)" ]; then cur=$$(cat "$(MODE_STAMP)"); fi; \
	if [ "$$cur" != "$$mode" ]; then \
		rm -f $(OBJS) $(PIC_OBJS) $(LIBNAME) $(SHLIB); \
		printf '%s\n' "$$mode" > "$(MODE_STAMP).tmp"; \
		mv "$(MODE_STAMP).tmp" "$(MODE_STAMP)"; \
	elif [ ! -f "$(MODE_STAMP)" ]; then \
		printf '%s\n' "$$mode" > "$(MODE_STAMP)"; \
	fi

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.pic.o: %.c
	$(CC) $(PIC_CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(PIC_OBJS) $(LIBNAME) $(SHLIB) $(MODE_STAMP)

install: $(LIBNAME) $(if $(NATIVE_BUILD),,$(SHLIB))
	@mkdir -p $(DESTDIR)/usr/lib $(DESTDIR)/lib
	@mkdir -p $(DESTDIR)/usr/include
	cp $(LIBNAME) $(DESTDIR)/usr/lib/
	cp ../../include/kvm.h $(DESTDIR)/usr/include/
ifndef NATIVE_BUILD
	install -m 755 $(SHLIB) $(DESTDIR)/lib/
	@ln -sf $(SHLIB) $(DESTDIR)/lib/libkvm.so
endif
