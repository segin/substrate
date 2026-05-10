include ../../Makefile.inc

CFLAGS := $(CFLAGS) -I.
LDFLAGS := $(USER_LDFLAGS)

SRCS = main.c yyerror.c
OBJS     = $(SRCS:.c=.o)
PIC_OBJS = $(SRCS:.c=.pic.o)

all: liby.a $(if $(NATIVE_BUILD),,liby.so.0)

liby.a: $(OBJS)
	$(AR) rcs $@ $(OBJS)

# liby has no DT_NEEDED — pure self-contained yacc runtime stubs.
liby.so.0: $(PIC_OBJS)
	$(CC) $(SHLIB_LDFLAGS) -Wl,-soname,liby.so.0 -o $@ $(PIC_OBJS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.pic.o: %.c
	$(CC) $(SHLIB_CFLAGS) -I. -c $< -o $@

clean:
	rm -f $(OBJS) $(PIC_OBJS) liby.a liby.so liby.so.0

install: liby.a $(if $(NATIVE_BUILD),,liby.so.0)
	mkdir -p $(DESTDIR)/usr/lib $(DESTDIR)/lib
	cp liby.a $(DESTDIR)/usr/lib/liby.a
ifndef NATIVE_BUILD
	install -m 755 liby.so.0 $(DESTDIR)/lib/
	@ln -sf liby.so.0 $(DESTDIR)/lib/liby.so
endif

.PHONY: all clean install
