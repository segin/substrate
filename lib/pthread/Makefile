include ../../Makefile.inc

CFLAGS    := $(CFLAGS) -I. -I../c/include -I../../include -I../../sys
PICFLAGS  := $(SHLIB_CFLAGS) -I. -I../c/include -I../../include -I../../sys
LDFLAGS   := $(USER_LDFLAGS)

SRCS = pthread_create.c pthread_mutex.c pthread_cond.c pthread_sig.c pthread_once.c pthread_extra.c semaphore.c
OBJS     = $(SRCS:.c=.o)
PIC_OBJS = $(SRCS:.c=.pic.o)

all: libpthread.a $(if $(NATIVE_BUILD),,libpthread.so.0)

libpthread.a: $(OBJS)
	$(AR) rcs $@ $(OBJS)

libpthread.so.0: $(PIC_OBJS)
	$(CC) $(SHLIB_LDFLAGS) -Wl,-soname,libpthread.so.0 \
	      -L$(TOP)/lib/c -l:libc.so.0 \
	      -o $@ $(PIC_OBJS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.pic.o: %.c
	$(CC) $(PICFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(PIC_OBJS) libpthread.a libpthread.so libpthread.so.0

install: libpthread.a $(if $(NATIVE_BUILD),,libpthread.so.0)
	@mkdir -p $(DESTDIR)/usr/lib $(DESTDIR)/lib
	install -m 644 libpthread.a $(DESTDIR)/usr/lib/
ifndef NATIVE_BUILD
	install -m 755 libpthread.so.0 $(DESTDIR)/lib/
	@ln -sf libpthread.so.0 $(DESTDIR)/lib/libpthread.so
endif
