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

all: $(O)libedit.a $(if $(NATIVE_BUILD),,$(O)libedit.so.0)

# $(O) and $(LIBDIR): see Makefile.inc (i386 in place, x86_64 beside it).
C_SRCS = editline.c readline.c terminal.c history.c signal.c keymap.c tokenizer.c rl_compat.c utf8.c
OBJS     = $(C_SRCS:%.c=$(O)%.o)
PIC_OBJS = $(C_SRCS:%.c=$(O)%.pic.o)

$(O)libedit.a: $(OBJS)
	$(AR) rcs $@ $^

# libedit needs libc for printf/malloc/etc.  DT_NEEDED must list
# libc.so.0 so the resolver pulls it in.
$(O)libedit.so.0: $(PIC_OBJS)
	$(CC) $(SHLIB_LDFLAGS) -Wl,-soname,libedit.so.0 \
	      -L$(TOP)/lib/c/$(O) -l:libc.so.0 \
	      -o $@ $(PIC_OBJS)

$(O)%.o: %.c el.h
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(O)%.pic.o: %.c el.h
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(PICFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(PIC_OBJS) $(O)libedit.a $(O)libedit.so $(O)libedit.so.0

install: $(O)libedit.a $(if $(NATIVE_BUILD),,$(O)libedit.so.0)
	@mkdir -p $(DESTDIR)/usr/$(LIBDIR) $(DESTDIR)/$(LIBDIR) $(DESTDIR)/usr/include
	install -m 644 $(O)libedit.a $(DESTDIR)/usr/$(LIBDIR)/
	install -m 644 ../../include/histedit.h $(DESTDIR)/usr/include/
ifndef NATIVE_BUILD
	install -m 755 $(O)libedit.so.0 $(DESTDIR)/$(LIBDIR)/
	@ln -sf libedit.so.0 $(DESTDIR)/$(LIBDIR)/libedit.so
endif
