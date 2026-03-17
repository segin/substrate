include ../../Makefile.inc

CFLAGS := $(CFLAGS) -I../../include -I../../sys/include
CXXFLAGS := $(CXXFLAGS) -I../../include -I../../sys/include
LDFLAGS := $(USER_LDFLAGS)

SRCS = src/compile.c src/exec.c src/util.c src/engine_posix.c src/engine_safe.c \
       src/engine_pcre2_adapter.c src/posix_compat.c
CPPSRCS = src/engine_re2_adapter.cpp

OBJS = $(SRCS:.c=.o) $(CPPSRCS:.cpp=.o)

LIBNAME = libregex.a

# Optional integrations
# USE_PCRE2=1 enables PCRE2 adapter (requires pcre2 headers/libs on host)
# USE_ICU=1 enables ICU-based unicode casefolding in the safe engine

ifeq ($(USE_PCRE2),1)
CFLAGS += -DREGEX_USE_PCRE2
endif

ifeq ($(USE_RE2),1)
CFLAGS += -DREGEX_USE_RE2
CXXFLAGS += -DREGEX_USE_RE2
endif

ifeq ($(DEFAULT_ENGINE_RE2),1)
CFLAGS += -DREGEX_DEFAULT_ENGINE_RE2
CXXFLAGS += -DREGEX_DEFAULT_ENGINE_RE2
endif
ifeq ($(USE_ICU),1)
CFLAGS += -DREGEX_USE_ICU
CXXFLAGS += -DREGEX_USE_ICU
endif

ifdef NATIVE_BUILD
CFLAGS += -DREGEX_USE_HOST_POSIX

# Fix CXX for native build (host)
HOSTCXX ?= c++
HOSTCXXFLAGS ?= -O2 -Wall
CXX := $(HOSTCXX)
# Use local include directory to avoid shadowing system headers with repo root includes
CXXFLAGS := $(HOSTCXXFLAGS) -Iinclude
# Ensure flags are preserved
ifeq ($(USE_RE2),1)
CXXFLAGS += -DREGEX_USE_RE2
endif
ifeq ($(DEFAULT_ENGINE_RE2),1)
CXXFLAGS += -DREGEX_DEFAULT_ENGINE_RE2
endif
ifeq ($(USE_ICU),1)
CXXFLAGS += -DREGEX_USE_ICU
endif

CXXFLAGS += -DREGEX_USE_HOST_POSIX
endif

.PHONY: all clean install

all: $(LIBNAME)

$(LIBNAME): $(OBJS)
	$(AR) rcs $@ $(OBJS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(LIBNAME)

install:
	@mkdir -p $(DESTDIR)/usr/lib
	@mkdir -p $(DESTDIR)/usr/include/regex
	@mkdir -p $(DESTDIR)/usr/lib/pkgconfig
	@mkdir -p $(DESTDIR)/usr/man/man3
	cp $(LIBNAME) $(DESTDIR)/usr/lib/
	cp ../../include/regex.h $(DESTDIR)/usr/include/
	cp ../../include/regex/flags.h $(DESTDIR)/usr/include/regex/
	cp regex.pc $(DESTDIR)/usr/lib/pkgconfig/
	if [ -f man3/regex.3 ]; then cp man3/regex.3 $(DESTDIR)/usr/man/man3/; fi
