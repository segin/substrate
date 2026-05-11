include ../../Makefile.inc

LIB    = libuu.a
SHLIB  = libuu.so.0
SRCS   = uudecode.c
OBJS     = $(SRCS:.c=.o)
PIC_OBJS = $(SRCS:.c=.pic.o)

all: $(LIB) $(if $(NATIVE_BUILD),,$(SHLIB))

$(LIB): $(OBJS)
	$(AR) rcs $@ $(OBJS)

$(SHLIB): $(PIC_OBJS)
	$(CC) $(SHLIB_LDFLAGS) -Wl,-soname,$(SHLIB) \
	      -L$(TOP)/lib/c -l:libc.so.0 \
	      -o $@ $(PIC_OBJS)

%.o: %.c
	$(CC) $(CFLAGS) -I. -c $< -o $@

%.pic.o: %.c
	$(CC) $(SHLIB_CFLAGS) -I. -c $< -o $@

clean:
	rm -f $(OBJS) $(PIC_OBJS) $(LIB) $(SHLIB)

install: $(LIB) $(if $(NATIVE_BUILD),,$(SHLIB))
	@mkdir -p $(DESTDIR)/usr/lib $(DESTDIR)/lib
	install -m 644 $(LIB) $(DESTDIR)/usr/lib/
ifndef NATIVE_BUILD
	install -m 755 $(SHLIB) $(DESTDIR)/lib/
	@ln -sf $(SHLIB) $(DESTDIR)/lib/libuu.so
endif
