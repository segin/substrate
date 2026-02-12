include ../../Makefile.inc

CFLAGS := $(CFLAGS) -I. -I../c/include -I../../sys
LDFLAGS := $(USER_LDFLAGS)

SRCS = pthread.c
OBJS = $(SRCS:.c=.o)

all: libpthread.a

libpthread.a: $(OBJS)
	$(AR) rcs $@ $(OBJS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) libpthread.a