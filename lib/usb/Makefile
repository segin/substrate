include ../../Makefile.inc

CFLAGS := $(CFLAGS) -Iinclude -I../../include -I../../sys/include -I../../sys

SRCS = src/libusb.c
OBJS = $(SRCS:.c=.o)

.PHONY: all clean

all: libusb.a

libusb.a: $(OBJS)
	$(AR) rcs $@ $(OBJS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) libusb.a