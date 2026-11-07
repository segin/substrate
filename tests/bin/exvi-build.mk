# exvi-build.mk -- ex and vi built for the host, for tests/bin/ex and
# tests/bin/vi: plain and with AddressSanitizer, from the sources, into
# this directory's build/.  Nothing outside it is built or cleaned.
#
# The tests used to `make -C usr.lib clean all` and rebuild bin/ex and
# bin/vi over the tree's own builds, target and host in turn, and to run
# against the plain build only -- some of the bugs they cover (EXVI-20,
# EXVI-21) show only under ASan.  libexvi is compiled against the host's
# <regex.h> and links the host's regex, so the two agree (EXVI-77).

TOP ?= ../../..
HOSTCC ?= cc
B = $(CURDIR)/build

EXVI_SRCS = $(wildcard $(TOP)/usr.lib/exvi/*.c)
EXVI_HDRS = $(wildcard $(TOP)/usr.lib/exvi/*.h) $(TOP)/include/exvi.h
EXVI_CFLAGS = -g -Wall -Wextra -Werror -idirafter $(TOP)/include \
              -idirafter $(TOP)/sys/include -idirafter $(TOP)/sys \
              -I$(TOP)/usr.lib/exvi
ASAN_CFLAGS = -O1 -fsanitize=address,undefined -fno-omit-frame-pointer

EX_PLAIN = $(B)/plain/ex
VI_PLAIN = $(B)/plain/vi
EX_ASAN  = $(B)/asan/ex
VI_ASAN  = $(B)/asan/vi

$(EX_PLAIN): $(TOP)/bin/ex/ex.c $(EXVI_SRCS) $(EXVI_HDRS)
	@mkdir -p $(@D)
	$(HOSTCC) -O2 $(EXVI_CFLAGS) -o $@ $(TOP)/bin/ex/ex.c $(EXVI_SRCS)

$(VI_PLAIN): $(TOP)/bin/vi/vi.c $(EXVI_SRCS) $(EXVI_HDRS)
	@mkdir -p $(@D)
	$(HOSTCC) -O2 $(EXVI_CFLAGS) -o $@ $(TOP)/bin/vi/vi.c $(EXVI_SRCS)

$(EX_ASAN): $(TOP)/bin/ex/ex.c $(EXVI_SRCS) $(EXVI_HDRS)
	@mkdir -p $(@D)
	$(HOSTCC) $(ASAN_CFLAGS) $(EXVI_CFLAGS) -o $@ $(TOP)/bin/ex/ex.c $(EXVI_SRCS)

$(VI_ASAN): $(TOP)/bin/vi/vi.c $(EXVI_SRCS) $(EXVI_HDRS)
	@mkdir -p $(@D)
	$(HOSTCC) $(ASAN_CFLAGS) $(EXVI_CFLAGS) -o $@ $(TOP)/bin/vi/vi.c $(EXVI_SRCS)

clean-build:
	rm -rf $(B)

.PHONY: clean-build
