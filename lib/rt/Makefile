include ../../Makefile.inc

# librt — POSIX realtime extensions.
#   * POSIX message queues (mq_*)   — thin wrappers over the kernel's six
#     native mq syscalls (sys/kern/posix_mqueue.c).
#   * POSIX asynchronous I/O (aio_*) — a userspace worker-thread pool over
#     libpthread (the glibc/librt model; no kernel support needed).
#
# Ships static (librt.a) and shared (librt.so.0) like the other libs; the
# shared object DT_NEEDEDs libpthread.so.0 (aio pool) and libc.so.0.

CFLAGS    := $(CFLAGS)   -I. -I../../include -I../../sys/include -I../../sys
PICFLAGS  := $(SHLIB_CFLAGS) -I. -I../../include -I../../sys/include -I../../sys
LDFLAGS   := $(USER_LDFLAGS)

SRCS     = mq.c aio.c
OBJS     = $(SRCS:.c=.o)
PIC_OBJS = $(SRCS:.c=.pic.o)

all: librt.a $(if $(NATIVE_BUILD),,librt.so.0)

librt.a: $(OBJS)
	$(AR) rcs $@ $(OBJS)

# librt uses libpthread (aio worker pool) and libc.  DT_NEEDED must list both
# so the resolver pulls them in.
librt.so.0: $(PIC_OBJS)
	$(CC) $(SHLIB_LDFLAGS) -Wl,-soname,librt.so.0 \
	      -L$(TOP)/lib/pthread -l:libpthread.so.0 \
	      -L$(TOP)/lib/c -l:libc.so.0 \
	      -o $@ $(PIC_OBJS)
	$(call BRAND_SUBSTRATE,$@)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.pic.o: %.c
	$(CC) $(PICFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(PIC_OBJS) librt.a librt.so librt.so.0

install: librt.a $(if $(NATIVE_BUILD),,librt.so.0)
	@mkdir -p $(DESTDIR)/usr/lib $(DESTDIR)/lib
	install -m 644 librt.a $(DESTDIR)/usr/lib/
ifndef NATIVE_BUILD
	install -m 755 librt.so.0 $(DESTDIR)/lib/
	@ln -sf librt.so.0 $(DESTDIR)/lib/librt.so
endif

.PHONY: all clean install
