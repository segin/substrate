include ../../Makefile.inc

CFLAGS := $(CFLAGS) -I../../include
LDFLAGS := $(USER_LDFLAGS)

SRCS = at_acl.c at_spool.c at_exec.c at_parse.c
OBJS = $(SRCS:.c=.o)

all: libat.a

libat.a: $(OBJS)
	$(AR) rcs $@ $(OBJS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) libat.a
