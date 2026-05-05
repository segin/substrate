HOSTCC ?= cc
WARNFLAGS ?= -Wall -Wextra -Werror
CFLAGS ?= -O2 -g
CPPFLAGS ?= -D_GNU_SOURCE
PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
LIBEXECDIR ?= $(PREFIX)/libexec/substrate

PROG = substrate-run

.PHONY: all clean install register unregister

all: $(PROG)

$(PROG): substrate-run.c
	$(HOSTCC) $(CPPFLAGS) $(CFLAGS) $(WARNFLAGS) -o $@ substrate-run.c

clean:
	rm -f $(PROG)

install: $(PROG)
	install -d "$(DESTDIR)$(BINDIR)"
	install -d "$(DESTDIR)$(LIBEXECDIR)"
	install -m 0755 $(PROG) "$(DESTDIR)$(BINDIR)/$(PROG)"
	install -m 0755 register-binfmt.sh "$(DESTDIR)$(LIBEXECDIR)/register-binfmt.sh"
	install -m 0755 unregister-binfmt.sh "$(DESTDIR)$(LIBEXECDIR)/unregister-binfmt.sh"
	install -m 0644 substrate-binfmt.conf "$(DESTDIR)$(LIBEXECDIR)/substrate-binfmt.conf"

register: install
	"$(DESTDIR)$(LIBEXECDIR)/register-binfmt.sh" --runner "$(BINDIR)/$(PROG)"

unregister:
	"$(LIBEXECDIR)/unregister-binfmt.sh"
