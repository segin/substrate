include ../../Makefile.inc

CFLAGS    := $(CFLAGS) -I.
PICFLAGS  := $(SHLIB_CFLAGS) -I.
LDFLAGS   := $(USER_LDFLAGS)

SRCS     = pwdb.c
OBJS     = $(SRCS:.c=.o)
PIC_OBJS = $(SRCS:.c=.pic.o)

all: libpwdb.a $(if $(NATIVE_BUILD),,libpwdb.so.0)

libpwdb.a: $(OBJS)
	$(AR) rcs $@ $(OBJS)

libpwdb.so.0: $(PIC_OBJS)
	$(CC) $(SHLIB_LDFLAGS) -Wl,-soname,libpwdb.so.0 \
	      -L$(TOP)/lib/c -l:libc.so.0 \
	      -o $@ $(PIC_OBJS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.pic.o: %.c
	$(CC) $(PICFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(PIC_OBJS) libpwdb.a libpwdb.so libpwdb.so.0

install: libpwdb.a $(if $(NATIVE_BUILD),,libpwdb.so.0)
	@mkdir -p $(DESTDIR)/usr/lib $(DESTDIR)/lib
	install -m 644 libpwdb.a $(DESTDIR)/usr/lib/
ifndef NATIVE_BUILD
	install -m 755 libpwdb.so.0 $(DESTDIR)/lib/
	@ln -sf libpwdb.so.0 $(DESTDIR)/lib/libpwdb.so
endif
