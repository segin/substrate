include ../../Makefile.inc

SRCS    = curses.c
OBJS    = $(SRCS:.c=.o)
PIC_OBJS = $(SRCS:.c=.pic.o)

all: libcurses.a $(if $(NATIVE_BUILD),,libcurses.so.0)

libcurses.a: $(OBJS)
	$(AR) rcs $@ $(OBJS)

libcurses.so.0: $(PIC_OBJS)
	$(CC) $(SHLIB_LDFLAGS) -Wl,-soname,libcurses.so.0 -o $@ $(PIC_OBJS)
	@# Patch OSABI to ELFOSABI_SUBSTRATE (0x40).
	@printf '\x40' | dd of=$@ bs=1 count=1 seek=7 conv=notrunc status=none

%.o: %.c
	$(CC) $(USER_CFLAGS) -I../../include -c $< -o $@

%.pic.o: %.c
	$(CC) $(SHLIB_CFLAGS) -I../../include -c $< -o $@

install: all
	@mkdir -p $(DESTDIR)/usr/lib $(DESTDIR)/usr/include
	cp libcurses.a $(DESTDIR)/usr/lib/
	cp libcurses.so.0 $(DESTDIR)/usr/lib/
	(cd $(DESTDIR)/usr/lib && ln -sf libcurses.so.0 libcurses.so)
	# Provide ncurses-name aliases so configure scripts that look for
	# -lncurses / -ltinfo find the same stub.
	(cd $(DESTDIR)/usr/lib && ln -sf libcurses.so.0 libncurses.so.0)
	(cd $(DESTDIR)/usr/lib && ln -sf libcurses.so.0 libncurses.so)
	(cd $(DESTDIR)/usr/lib && ln -sf libcurses.a libncurses.a)
	(cd $(DESTDIR)/usr/lib && ln -sf libcurses.so.0 libtinfo.so.0)
	(cd $(DESTDIR)/usr/lib && ln -sf libcurses.so.0 libtinfo.so)
	(cd $(DESTDIR)/usr/lib && ln -sf libcurses.a libtinfo.a)
	# The stub headers live here rather than in the top-level include/.
	# include/ is copied into the cross sysroot and the image
	# unconditionally, so a stub there shadowed ncurses's real
	# curses.h/term.h/termcap.h even though this library is never built
	# or installed alongside contrib/ncurses.  Keeping them with the stub
	# means they appear exactly when the stub itself does.
	cp curses.h term.h termcap.h $(DESTDIR)/usr/include/

clean:
	rm -f libcurses.a libcurses.so.0 $(OBJS) $(PIC_OBJS)

.PHONY: all install clean
