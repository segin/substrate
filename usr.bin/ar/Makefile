include ../../Makefile.inc

PROG = ar
SRCS = ar.c
ELFOBJ_LIB = ../../usr.lib/elfobj/libelfobj.a
LDADD += $(ELFOBJ_LIB)

# Include path for elfobj.h
CFLAGS += -iquote $(TOP)/include

# Links for ranlib
LINKS = $(DESTDIR)/usr/bin/ar $(DESTDIR)/usr/bin/ranlib

all: $(PROG)

$(PROG): $(SRCS) $(ELFOBJ_LIB)
	$(CC) $(CFLAGS) -o $@ $(SRCS) $(LDADD)

$(ELFOBJ_LIB):
	$(MAKE) -C ../../usr.lib/elfobj $(if $(NATIVE_BUILD),NATIVE_BUILD=1)

clean:
	rm -f $(PROG) $(PROG).o

install: $(PROG)
	@echo "Installing $(PROG)..."
	@mkdir -p $(DESTDIR)/usr/bin
	@cp $(PROG) $(DESTDIR)/usr/bin/$(PROG)
	@ln -sf $(PROG) $(DESTDIR)/usr/bin/ranlib
