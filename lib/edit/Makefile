include ../../Makefile.inc

CC = gcc
ifdef NATIVE_BUILD
CFLAGS := -Wall -g
CPPFLAGS := -include ../../include/histedit.h
else
CFLAGS += -I../../include -I../../sys/include -I../../sys
endif

.PHONY: all clean install

ifneq ($(filter clean,$(MAKECMDGOALS)),)
ifneq ($(filter all,$(MAKECMDGOALS)),)
all: clean
.NOTPARALLEL: clean all
endif
endif

all: libedit.a

OBJS = editline.o readline.o terminal.o history.o signal.o keymap.o tokenizer.o rl_compat.o utf8.o

libedit.a: $(OBJS)
	$(AR) rcs $@ $^

%.o: %.c el.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

clean:
	rm -f *.o libedit.a

install: libedit.a
	@mkdir -p $(DESTDIR)/usr/lib
	@mkdir -p $(DESTDIR)/usr/include
	cp libedit.a $(DESTDIR)/usr/lib/
	cp ../../include/histedit.h $(DESTDIR)/usr/include/
