include ../../Makefile.inc

CFLAGS := $(CFLAGS) -I.
LDFLAGS := $(USER_LDFLAGS)

SRCS = main.c yyerror.c
OBJS = $(SRCS:.c=.o)

all: liby.a

liby.a: $(OBJS)
	$(AR) rcs $@ $(OBJS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) liby.a

install: liby.a
	mkdir -p $(DESTDIR)/usr/lib
	cp liby.a $(DESTDIR)/usr/lib/liby.a

.PHONY: all clean install
