include ../../Makefile.inc

SRCS = num.c arith.c
OBJS = $(SRCS:.c=.o)

all: libbc.a

libbc.a: $(OBJS)
	$(AR) rcs $@ $(OBJS)

%.o: %.c
	$(CC) $(CFLAGS) -I../../include -c $< -o $@

clean:
	rm -f $(OBJS) libbc.a
