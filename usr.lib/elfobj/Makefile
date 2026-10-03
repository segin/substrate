include ../../Makefile.inc

ifeq ($(NATIVE_BUILD),1)
CFLAGS := $(CFLAGS) -idirafter ../../include -idirafter ../../sys -idirafter ../../sys/include -I./src
PIC_CFLAGS := $(CFLAGS)
else
CFLAGS := $(CFLAGS) -I../../include -I../../sys -I../../sys/include -I./src
PIC_CFLAGS := $(SHLIB_CFLAGS) -I../../include -I../../sys -I../../sys/include -I./src
endif

# $(O) and $(LIBDIR): see Makefile.inc (i386 in place, x86_64 beside it).
# LIBNAME and SHLIB stay bare names (soname, install link); the files
# themselves are $(O)$(LIBNAME) and $(O)$(SHLIB).
LIBNAME = libelfobj.a
SHLIB   = libelfobj.so.0
SRCS = src/elf_read.c src/elf_write.c src/elf_sections.c src/elf_symbols.c \
       src/elf_reloc.c src/elf_link.c src/elf_strtab.c src/elf_hash.c \
       src/elf_gnu_ext.c src/elf_dwarf.c src/elf_dwarf_line.c src/elf_validate.c src/elf_layout.c \
       src/elf_util.c
OBJS     = $(SRCS:%.c=$(O)%.o)
PIC_OBJS = $(SRCS:%.c=$(O)%.pic.o)
BUILD_MODE := $(if $(NATIVE_BUILD),native,target)
MODE_STAMP = $(O).build-mode

.PHONY: all clean install test fuzz bench examples FORCE

all: $(O)$(LIBNAME) $(if $(NATIVE_BUILD),,$(O)$(SHLIB))

$(O)$(LIBNAME): $(OBJS)
	$(AR) rcs $@ $(OBJS)

# libelfobj is a leaf library (no DT_NEEDED beyond libc).
$(O)$(SHLIB): $(PIC_OBJS)
	$(CC) $(SHLIB_LDFLAGS) -Wl,-soname,$(SHLIB) \
	      -L$(TOP)/lib/c/$(O) -l:libc.so.0 \
	      -o $@ $(PIC_OBJS)

$(OBJS) $(PIC_OBJS): $(MODE_STAMP)

$(MODE_STAMP): FORCE
	@mkdir -p $(dir $@)
	@mode="$(BUILD_MODE)"; \
	cur=""; \
	if [ -f "$(MODE_STAMP)" ]; then cur=$$(cat "$(MODE_STAMP)"); fi; \
	if [ "$$cur" != "$$mode" ]; then \
		rm -f $(OBJS) $(PIC_OBJS) $(O)$(LIBNAME) $(O)$(SHLIB); \
		printf '%s\n' "$$mode" > "$(MODE_STAMP).tmp"; \
		mv "$(MODE_STAMP).tmp" "$(MODE_STAMP)"; \
	elif [ ! -f "$(MODE_STAMP)" ]; then \
		printf '%s\n' "$$mode" > "$(MODE_STAMP)"; \
	fi

$(O)%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(O)%.pic.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(PIC_CFLAGS) -c $< -o $@

test: $(LIBNAME)
	$(MAKE) -C ../../tests/usr.lib/elfobj NATIVE_BUILD=1

fuzz: $(LIBNAME)
	$(MAKE) -C fuzz NATIVE_BUILD=1

bench: $(LIBNAME)
	$(MAKE) -C bench NATIVE_BUILD=1

examples: $(LIBNAME)
	$(MAKE) -C examples NATIVE_BUILD=1

clean:
	rm -f $(OBJS) $(PIC_OBJS) $(O)$(LIBNAME) $(O)$(SHLIB) $(MODE_STAMP)
	$(MAKE) -C ../../tests/usr.lib/elfobj clean || true
	$(MAKE) -C fuzz clean || true
	$(MAKE) -C bench clean || true
	$(MAKE) -C examples clean || true

install: $(O)$(LIBNAME) $(if $(NATIVE_BUILD),,$(O)$(SHLIB))
	@mkdir -p $(DESTDIR)/usr/$(LIBDIR) $(DESTDIR)/$(LIBDIR)
	@mkdir -p $(DESTDIR)/usr/include
	@mkdir -p $(DESTDIR)/usr/$(LIBDIR)/pkgconfig
	cp $(O)$(LIBNAME) $(DESTDIR)/usr/$(LIBDIR)/
	cp ../../include/elfobj.h $(DESTDIR)/usr/include/
	cp elfobj.pc $(DESTDIR)/usr/$(LIBDIR)/pkgconfig/
	cp libelfobj.map $(DESTDIR)/usr/$(LIBDIR)/
ifndef NATIVE_BUILD
	install -m 755 $(O)$(SHLIB) $(DESTDIR)/$(LIBDIR)/
	@ln -sf $(SHLIB) $(DESTDIR)/$(LIBDIR)/libelfobj.so
endif
