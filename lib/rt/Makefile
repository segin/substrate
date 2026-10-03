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

# $(O) and $(LIBDIR): see Makefile.inc (i386 in place, x86_64 beside it).
SRCS     = mq.c aio.c
OBJS     = $(SRCS:%.c=$(O)%.o)
PIC_OBJS = $(SRCS:%.c=$(O)%.pic.o)

all: $(O)librt.a $(if $(NATIVE_BUILD),,$(O)librt.so.0)

$(O)librt.a: $(OBJS)
	$(AR) rcs $@ $(OBJS)

# librt uses libpthread (aio worker pool) and libc.  DT_NEEDED must list both
# so the resolver pulls them in.
$(O)librt.so.0: $(PIC_OBJS)
	$(CC) $(SHLIB_LDFLAGS) -Wl,-soname,librt.so.0 \
	      -L$(TOP)/lib/pthread/$(O) -l:libpthread.so.0 \
	      -L$(TOP)/lib/c/$(O) -l:libc.so.0 \
	      -o $@ $(PIC_OBJS)
	$(call BRAND_SUBSTRATE,$@)

$(O)%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(O)%.pic.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(PICFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(PIC_OBJS) $(O)librt.a $(O)librt.so $(O)librt.so.0

install: $(O)librt.a $(if $(NATIVE_BUILD),,$(O)librt.so.0)
	@mkdir -p $(DESTDIR)/usr/$(LIBDIR) $(DESTDIR)/$(LIBDIR)
	install -m 644 $(O)librt.a $(DESTDIR)/usr/$(LIBDIR)/
ifndef NATIVE_BUILD
	install -m 755 $(O)librt.so.0 $(DESTDIR)/$(LIBDIR)/
	@ln -sf librt.so.0 $(DESTDIR)/$(LIBDIR)/librt.so
endif

.PHONY: all clean install
