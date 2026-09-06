PROG = join
SRCS = join.c

DYNAMIC = 1

include ../../Makefile.bin.inc

# join's field/table logic lives in usr.lib/join so the tests can exercise
# it directly.  Link the shared object by its exact file name: there is no
# libjoin.so link name in the target build tree, so a plain -ljoin resolves
# to libjoin.a instead -- silently, because the static archive links fine.
# The host build has only the archive, so it takes the ordinary -l form.
ifdef NATIVE_BUILD
JOINLIB = $(TOP)/usr.lib/join/libjoin.a
LDADD   = -L$(TOP)/usr.lib/join -ljoin
USER_CFLAGS += -idirafter $(TOP)/include
else
JOINLIB = $(TOP)/usr.lib/join/libjoin.so.0
LDADD   = -L$(TOP)/usr.lib/join -l:libjoin.so.0
endif

$(PROG): $(JOINLIB)

# Recurse unconditionally and let usr.lib/join decide.  A rule that fires
# only when the library is MISSING leaves a stale one in place, and this
# library is built in two incompatible modes: after a target build,
# libjoin.a holds i386 objects, and a `make NATIVE_BUILD=1` that skipped
# the sub-make would hand them to the host linker --
#
#     skipping incompatible libjoin.a when searching for -ljoin
#
# usr.lib/join keeps a .build-mode stamp and wipes its objects when the
# mode changes, but only if it is entered.  join relinks only when the
# sub-make actually touches the library.
.PHONY: FORCE
FORCE:

$(JOINLIB): FORCE
	$(MAKE) -C $(TOP)/usr.lib/join NATIVE_BUILD=$(NATIVE_BUILD)
