include ../../Makefile.inc

ifdef NATIVE_BUILD
CFLAGS := $(CFLAGS) -DNATIVE_BUILD=1 -Iinclude -idirafter ../../sys/include -I../../sys
else
CFLAGS := $(CFLAGS) -Iinclude -I../../include -I../../sys/include -I../../sys
endif

SRCS = src/core.c src/device.c src/sync.c src/descriptor.c src/async.c src/poll.c
OBJS = $(SRCS:.c=.o)

.PHONY: all clean

all: libusb.a

libusb.a: $(OBJS)
	$(AR) rcs $@ $(OBJS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) libusb.a