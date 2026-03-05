CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -g

INCLUDES = -I../../../include -I../../../sys/include
LIBREGEX = ../../../usr.lib/regex/libregex.a
LIBS =

ifeq ($(USE_RE2),1)
CFLAGS += -DREGEX_USE_RE2
LIBS += -lre2 -lstdc++
endif

ifeq ($(DEFAULT_ENGINE_RE2),1)
CFLAGS += -DREGEX_DEFAULT_ENGINE_RE2
endif

SRCS = test_main.c \
       unit/test_api.c \
       unit/test_util.c \
       integration/test_replace.c \
       integration/test_split.c \
       security/test_dos.c \
       streaming/test_streaming.c \
       encoding/test_utf8.c

OBJS = $(SRCS:.c=.o)

.PHONY: all clean run

all: test_regex

test_regex: $(OBJS) $(LIBREGEX)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $(OBJS) $(LIBREGEX) $(LIBS)

$(LIBREGEX):
	$(MAKE) -C ../../../usr.lib/regex NATIVE_BUILD=1 USE_RE2=$(USE_RE2) DEFAULT_ENGINE_RE2=$(DEFAULT_ENGINE_RE2)

%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

run: test_regex
	./test_regex

clean:
	rm -f $(OBJS) test_regex
