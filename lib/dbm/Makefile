include ../../Makefile.inc

CFLAGS := $(CFLAGS) -I.
LDFLAGS := $(USER_LDFLAGS)

SRCS = dbm.c
OBJS = $(SRCS:.c=.o)

all: libdbm.a

libdbm.a: $(OBJS)
	$(AR) rcs $@ $(OBJS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) libdbm.a
