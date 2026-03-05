include ../../Makefile.inc

ifeq ($(NATIVE_BUILD),1)
CFLAGS := $(CFLAGS) -idirafter ../../include
else
CFLAGS := $(CFLAGS) -I../../include
endif

PROG = join
SRCS = join.c
OBJS = $(SRCS:.c=.o)

.PHONY: all clean install

all: $(PROG)

$(PROG): $(OBJS)
	@if [ ! -f ../../usr.lib/join/libjoin.so ]; then $(MAKE) -C ../../usr.lib/join NATIVE_BUILD=$(NATIVE_BUILD); fi
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDFLAGS) -L../../usr.lib/join -ljoin

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(PROG)

install: $(PROG)
	@mkdir -p $(DESTDIR)/bin
	cp $(PROG) $(DESTDIR)/bin/
