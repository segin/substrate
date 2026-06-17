PROG = grep
SRCS = grep.c grep_opts.c grep_pattern.c
EXTRA_CLEAN = egrep fgrep hostshim
.DEFAULT_GOAL := all

# Link the in-tree regex engine.  The target build uses the shared object and
# substrate's own headers; the host (NATIVE_BUILD=1) test build uses the host
# static archive so the same sources verify off-target.
#
# CRITICAL (see CLAUDE.md): a host build must take its SYSTEM headers (sys/stat
# struct layout, dirent, etc.) from the HOST libc it links against -- mixing
# substrate's sys/include with host libc gives a struct-stat size mismatch and
# a stack-smash in stat().  So the host build sees only a regex-only include
# shim (symlinks to the two substrate regex headers), and everything else
# resolves to the host.
ifdef NATIVE_BUILD
REGEX_LIB = $(TOP)/usr.lib/regex/libregex.a
LDADD = $(REGEX_LIB)
$(shell mkdir -p hostshim/regex && \
        ln -sf $(abspath ../../include/regex.h) hostshim/regex.h && \
        ln -sf $(abspath ../../include/regex/flags.h) hostshim/regex/flags.h)
USER_CFLAGS += -Ihostshim
else
REGEX_LIB = $(TOP)/usr.lib/regex/libregex.so.0
LDADD = -L$(TOP)/usr.lib/regex -l:libregex.so.0
USER_CFLAGS += -I$(TOP)/include -I$(TOP)/sys/include
endif

DYNAMIC = 1

include ../../Makefile.inc
include ../../Makefile.bin.inc

# Force the regex sub-make so its build-mode stamp can switch the archive
# between host and target as needed.
.PHONY: regexlib
regexlib:
	$(MAKE) -C $(TOP)/usr.lib/regex $(if $(NATIVE_BUILD),NATIVE_BUILD=1,)

$(REGEX_LIB): regexlib

$(PROG): $(REGEX_LIB)

# egrep / fgrep are the same binary, selected by argv[0] (REQ-GREP-004/005).
all: grep-links

grep-links: $(PROG)
	ln -sf $(PROG) egrep
	ln -sf $(PROG) fgrep

# egrep / fgrep are installed as shebang wrapper scripts rather than
# symlinks: the image is populated from dist/ via debugfs `write`, which
# stages regular files reliably but not relative symlinks, so a symlinked
# egrep silently went missing on-target (starttde: "command not found:
# egrep").  A wrapper is a plain file and always lands.
install-grep-links:
	install -d $(BINDIR)
	printf '#!/bin/sh\n# egrep -- grep in extended-regexp mode.\nexec grep -E "$$@"\n' > $(BINDIR)/egrep
	printf '#!/bin/sh\n# fgrep -- grep in fixed-string mode.\nexec grep -F "$$@"\n' > $(BINDIR)/fgrep
	chmod 0755 $(BINDIR)/egrep $(BINDIR)/fgrep

install: install-grep-links
