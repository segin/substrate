include ../../Makefile.inc

PROG = c++filt
SRCS = cxxfilt.c
OBJS = $(SRCS:.c=.o)

LDADD += ../../usr.lib/demangle/libdemangle.a

.PHONY: all clean install

all: $(PROG)

$(PROG): $(OBJS) ../../usr.lib/demangle/libdemangle.a
ifdef NATIVE_BUILD
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDADD)
else
	$(CC) $(USER_LDFLAGS) -o $@ $(OBJS) $(TOP)/lib/c/crt0.o $(LDADD) -L$(TOP)/lib/c -lc $(LIBGCC)
	$(call BRAND_SUBSTRATE,$@)
endif

../../usr.lib/demangle/libdemangle.a:
	$(MAKE) -C ../../usr.lib/demangle $(if $(NATIVE_BUILD),NATIVE_BUILD=1)

%.o: %.c
ifdef NATIVE_BUILD
	$(CC) $(CFLAGS) -idirafter ../../include -c $< -o $@
else
	$(CC) $(USER_CFLAGS) -c $< -o $@
endif

clean:
	rm -f $(PROG) $(OBJS)

install: $(PROG)
	@mkdir -p $(DESTDIR)/usr/bin
	cp $(PROG) $(DESTDIR)/usr/bin/$(PROG)
