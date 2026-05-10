include ../../Makefile.inc

ifdef NATIVE_BUILD
CFLAGS := -Wall -g
CPPFLAGS := -include ../../include/histedit.h
else
# Static-link CFLAGS keep -fno-pie via USER_CFLAGS' policy; the
# .pic.o pass uses SHLIB_CFLAGS instead.
CFLAGS    := $(USER_CFLAGS)  -I../../include -I../../sys/include -I../../sys
PICFLAGS  := $(SHLIB_CFLAGS) -I../../include -I../../sys/include -I../../sys
endif

.PHONY: all clean install

ifneq ($(filter clean,$(MAKECMDGOALS)),)
ifneq ($(filter all,$(MAKECMDGOALS)),)
all: clean
.NOTPARALLEL: clean all
endif
endif

all: libedit.a $(if $(NATIVE_BUILD),,libedit.so.0)

C_SRCS = editline.c readline.c terminal.c history.c signal.c keymap.c tokenizer.c rl_compat.c utf8.c
OBJS     = $(C_SRCS:.c=.o)
PIC_OBJS = $(C_SRCS:.c=.pic.o)

libedit.a: $(OBJS)
	$(AR) rcs $@ $^

# libedit needs libc for printf/malloc/etc.  DT_NEEDED must list
# libc.so.0 so the resolver pulls it in.
libedit.so.0: $(PIC_OBJS)
	$(CC) $(SHLIB_LDFLAGS) -Wl,-soname,libedit.so.0 \
	      -L$(TOP)/lib/c -l:libc.so.0 \
	      -o $@ $(PIC_OBJS)

%.o: %.c el.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

%.pic.o: %.c el.h
	$(CC) $(CPPFLAGS) $(PICFLAGS) -c -o $@ $<

clean:
	rm -f *.o *.pic.o libedit.a libedit.so libedit.so.0

install: libedit.a $(if $(NATIVE_BUILD),,libedit.so.0)
	@mkdir -p $(DESTDIR)/usr/lib $(DESTDIR)/lib $(DESTDIR)/usr/include
	install -m 644 libedit.a $(DESTDIR)/usr/lib/
	install -m 644 ../../include/histedit.h $(DESTDIR)/usr/include/
ifndef NATIVE_BUILD
	install -m 755 libedit.so.0 $(DESTDIR)/lib/
	@ln -sf libedit.so.0 $(DESTDIR)/lib/libedit.so
endif
