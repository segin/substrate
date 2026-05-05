CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -g -D_GNU_SOURCE

INCLUDES = -I. -I../../../include

SRCS = test_main.c \
       test_liblink.c

OBJS = $(SRCS:.c=.o)

.PHONY: all clean run

all: test_link

test_link: $(OBJS)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $(OBJS)

%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

run: test_link
	./test_link

clean:
	rm -f $(OBJS) test_link
