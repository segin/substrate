include ../../Makefile.inc

SRCS = buffer.c demangle.c itanium.c rust.c dlang.c
OBJS = $(SRCS:.c=.o)
LIBNAME = libdemangle.a

CFLAGS += -I../../include

.PHONY: all clean install

all: $(LIBNAME)

$(LIBNAME): $(OBJS)
	$(AR) rcs $@ $(OBJS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(LIBNAME)

install: $(LIBNAME)
	@mkdir -p $(DESTDIR)/usr/lib
	@mkdir -p $(DESTDIR)/usr/include
	cp $(LIBNAME) $(DESTDIR)/usr/lib/
	cp ../../include/demangle.h $(DESTDIR)/usr/include/
