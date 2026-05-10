include ../../Makefile.inc

CFLAGS    := $(CFLAGS) -I.
PICFLAGS  := $(SHLIB_CFLAGS) -I.
LDFLAGS   := $(USER_LDFLAGS)

SRCS = dbm.c
OBJS     = $(SRCS:.c=.o)
PIC_OBJS = $(SRCS:.c=.pic.o)

all: libdbm.a $(if $(NATIVE_BUILD),,libdbm.so.0)

libdbm.a: $(OBJS)
	$(AR) rcs $@ $(OBJS)

libdbm.so.0: $(PIC_OBJS)
	$(CC) $(SHLIB_LDFLAGS) -Wl,-soname,libdbm.so.0 \
	      -L$(TOP)/lib/c -l:libc.so.0 \
	      -o $@ $(PIC_OBJS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.pic.o: %.c
	$(CC) $(PICFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(PIC_OBJS) libdbm.a libdbm.so libdbm.so.0

install: libdbm.a $(if $(NATIVE_BUILD),,libdbm.so.0)
	@mkdir -p $(DESTDIR)/usr/lib $(DESTDIR)/lib
	install -m 644 libdbm.a $(DESTDIR)/usr/lib/
ifndef NATIVE_BUILD
	install -m 755 libdbm.so.0 $(DESTDIR)/lib/
	@ln -sf libdbm.so.0 $(DESTDIR)/lib/libdbm.so
endif
