include ../../Makefile.inc

CC = gcc
ifdef NATIVE_BUILD
CFLAGS := -Wall -g -I.
else
CFLAGS += -I../../include -I../../sys/include -I../../sys
endif

all: histedit.h libedit.a

histedit.h: ../../include/histedit.h
	cp $< $@

OBJS = editline.o readline.o terminal.o history.o signal.o keymap.o tokenizer.o rl_compat.o utf8.o

libedit.a: $(OBJS)
	$(AR) rcs $@ $^

%.o: %.c histedit.h el.h
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f *.o libedit.a histedit.h

install: histedit.h libedit.a
	@mkdir -p $(DESTDIR)/usr/lib
	@mkdir -p $(DESTDIR)/usr/include
	cp libedit.a $(DESTDIR)/usr/lib/
	cp ../../include/histedit.h $(DESTDIR)/usr/include/
