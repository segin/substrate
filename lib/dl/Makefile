include ../../Makefile.inc

# Thin POSIX wrappers around /sbin/ld.so's __ldso_dl* exports.
# Builds both libdl.a (for static-linked binaries that won't ever
# call into ld.so) and libdl.so.0 (for dynamic binaries).

CFLAGS    := $(USER_CFLAGS) -I../../include
PICFLAGS  := $(SHLIB_CFLAGS) -I../../include

SRCS = dl.c
OBJS     = $(SRCS:.c=.o)
PIC_OBJS = $(SRCS:.c=.pic.o)

.PHONY: all clean install

all: libdl.a $(if $(NATIVE_BUILD),,libdl.so.0)

libdl.a: $(OBJS)
	$(AR) rcs $@ $(OBJS)

# libdl has no DT_NEEDED — the __ldso_dl* references are weak and
# resolve at runtime against /sbin/ld.so itself (which is always
# loaded for any dynamic binary that pulled libdl in).
libdl.so.0: $(PIC_OBJS)
	$(CC) $(SHLIB_LDFLAGS) -Wl,-soname,libdl.so.0 \
	      -Wl,--unresolved-symbols=ignore-all \
	      -o $@ $(PIC_OBJS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.pic.o: %.c
	$(CC) $(PICFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(PIC_OBJS) libdl.a libdl.so.0

install: libdl.a $(if $(NATIVE_BUILD),,libdl.so.0)
	@mkdir -p $(DESTDIR)/usr/lib $(DESTDIR)/lib
	install -m 644 libdl.a $(DESTDIR)/usr/lib/
ifndef NATIVE_BUILD
	install -m 755 libdl.so.0 $(DESTDIR)/lib/
	@ln -sf libdl.so.0 $(DESTDIR)/lib/libdl.so
endif
