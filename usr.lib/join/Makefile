include ../../Makefile.inc

ifeq ($(NATIVE_BUILD),1)
CFLAGS := $(CFLAGS) -idirafter ../../include -idirafter ../../sys -idirafter ../../sys/include -fPIC
else
CFLAGS := $(CFLAGS) -I../../include -I../../sys -I../../sys/include -fPIC
endif

LIBNAME_A = libjoin.a
LIBNAME_SO = libjoin.so
SRCS = libjoin.c
OBJS = $(SRCS:.c=.o)

.PHONY: all clean install

all: $(LIBNAME_A) $(LIBNAME_SO)

$(LIBNAME_A): $(OBJS)
	$(AR) rcs $@ $(OBJS)

$(LIBNAME_SO): $(OBJS)
	$(CC) -shared -o $@ $(OBJS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(LIBNAME_A) $(LIBNAME_SO)

install: $(LIBNAME_A) $(LIBNAME_SO)
	@mkdir -p $(DESTDIR)/usr/lib
	@mkdir -p $(DESTDIR)/usr/include
	cp $(LIBNAME_A) $(LIBNAME_SO) $(DESTDIR)/usr/lib/
	cp ../../include/libjoin.h $(DESTDIR)/usr/include/
