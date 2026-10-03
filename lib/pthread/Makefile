include ../../Makefile.inc

CFLAGS    := $(CFLAGS) -I. -I../c/include -I../../include -I../../sys
PICFLAGS  := $(SHLIB_CFLAGS) -I. -I../c/include -I../../include -I../../sys
LDFLAGS   := $(USER_LDFLAGS)

# $(O) and $(LIBDIR): see Makefile.inc (i386 in place, x86_64 beside it).
SRCS = pthread_create.c pthread_mutex.c pthread_cond.c pthread_sig.c pthread_once.c pthread_extra.c pthread_barrier.c pthread_spin.c pthread_atfork.c pthread_cancel.c semaphore.c
OBJS     = $(SRCS:%.c=$(O)%.o)
PIC_OBJS = $(SRCS:%.c=$(O)%.pic.o)

all: $(O)libpthread.a $(if $(NATIVE_BUILD),,$(O)libpthread.so.0)

$(O)libpthread.a: $(OBJS)
	$(AR) rcs $@ $(OBJS)

$(O)libpthread.so.0: $(PIC_OBJS)
	$(CC) $(SHLIB_LDFLAGS) -Wl,-soname,libpthread.so.0 \
	      -L$(TOP)/lib/c/$(O) -l:libc.so.0 \
	      -o $@ $(PIC_OBJS)

$(O)%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(O)%.pic.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(PICFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(PIC_OBJS) $(O)libpthread.a $(O)libpthread.so $(O)libpthread.so.0

install: $(O)libpthread.a $(if $(NATIVE_BUILD),,$(O)libpthread.so.0)
	@mkdir -p $(DESTDIR)/usr/$(LIBDIR) $(DESTDIR)/$(LIBDIR)
	install -m 644 $(O)libpthread.a $(DESTDIR)/usr/$(LIBDIR)/
ifndef NATIVE_BUILD
	install -m 755 $(O)libpthread.so.0 $(DESTDIR)/$(LIBDIR)/
	@ln -sf libpthread.so.0 $(DESTDIR)/$(LIBDIR)/libpthread.so
endif
