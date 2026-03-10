include ../../Makefile.inc

CFLAGS := $(CFLAGS) -Iinclude -I../../include -I$(TOP)/sys
LDFLAGS := $(USER_LDFLAGS)

SRCS = src/sys.c src/stdlib.c src/string.c src/ctype.c src/assert.c src/dirent.c \
       src/pwd.c src/grp.c src/time/time.c src/div64.c fnmatch.c src/getopt.c src/wchar.c src/libgen.c \
       stdio/stdio_core.c stdio/stdstreams.c stdio/printf.c sys_generic.c src/sysctl_helpers.c
ASM_SRCS = arch/i386/syscall.S arch/i386/crt0.S

OBJS = $(SRCS:.c=.o) $(ASM_SRCS:.S=.o)

all: libc.a crt0.o

libc.a: $(OBJS)
	$(AR) rcs $@ $(filter-out arch/i386/crt0.o, $(OBJS))

crt0.o: arch/i386/crt0.o
	cp $< $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.S
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) libc.a crt0.o
