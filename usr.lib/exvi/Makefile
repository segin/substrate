include ../../Makefile.inc

LIB = libexvi.a
SRCS = exvi.c
OBJS = $(SRCS:.c=.o)

ifeq ($(NATIVE_BUILD),1)
CFLAGS := $(CFLAGS) -idirafter ../../include -idirafter ../../sys/include -idirafter ../../sys
else
CFLAGS := $(CFLAGS) -I../../include -I../../sys/include -I../../sys
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
	cp ../../include/exvi.h $(DESTDIR)/usr/include/
