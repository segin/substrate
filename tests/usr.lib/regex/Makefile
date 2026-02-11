CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -g

INCLUDES = -I../../include -I../../sys/include
LIBREGEX = ../../usr.lib/regex/libregex.a

SRCS = test_main.c \
       unit/test_api.c \
       integration/test_replace.c \
       security/test_dos.c \
       streaming/test_streaming.c \
       encoding/test_utf8.c

OBJS = $(SRCS:.c=.o)

.PHONY: all clean run

all: test_regex

test_regex: $(OBJS) $(LIBREGEX)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $(OBJS) $(LIBREGEX)

$(LIBREGEX):
	$(MAKE) -C ../../usr.lib/regex

%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

run: test_regex
	./test_regex

clean:
	rm -f $(OBJS) test_regex
