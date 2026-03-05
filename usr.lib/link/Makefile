include ../../Makefile.inc

LIB = liblink.a
SRCS = liblink.c
OBJS = $(SRCS:.c=.o)

ifeq ($(NATIVE_BUILD),1)
CFLAGS := $(CFLAGS) -idirafter ../../include -D_GNU_SOURCE -DLINK_HAVE_SYMLINK=1
else
CFLAGS := $(CFLAGS) -I../../include
endif

.PHONY: all clean install

all: $(LIB)

$(LIB): $(OBJS)
	$(AR) rcs $@ $(OBJS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(LIB)

install: $(LIB)
	@mkdir -p $(DESTDIR)/usr/lib
	@mkdir -p $(DESTDIR)/usr/include
	cp $(LIB) $(DESTDIR)/usr/lib/
	cp ../../include/liblink.h $(DESTDIR)/usr/include/
