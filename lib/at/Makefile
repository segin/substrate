include ../../Makefile.inc

CFLAGS := $(CFLAGS) -I../../include
LDFLAGS := $(USER_LDFLAGS)

SRCS = at_acl.c at_spool.c at_exec.c at_parse.c
OBJS = at_acl.o at_spool.o at_exec.o at_parse.o at_parse_y.o lex.yy.o
CLEANFILES = $(OBJS) libat.a at_parse_y.c at_parse_y.h lex.yy.c at_parse.tab.c at_parse.tab.h

all: libat.a

at_parse_y.c at_parse_y.h: at_parse.y
	../../usr.bin/yacc/yacc -d -b at_parse at_parse.y
	mv at_parse.tab.c at_parse_y.c
	mv at_parse.tab.h at_parse_y.h

lex.yy.c: at_parse.l at_parse_y.h
	../../usr.bin/lex/lex at_parse.l

at_parse_y.o: at_parse_y.c
	$(CC) $(CFLAGS) -Wno-implicit-function-declaration -Wno-int-conversion -Wno-incompatible-pointer-types -c at_parse_y.c -o $@

lex.yy.o: lex.yy.c
	$(CC) $(CFLAGS) -Wno-implicit-function-declaration -Wno-int-conversion -Wno-incompatible-pointer-types -c lex.yy.c -o $@

at_parse.o: at_parse.c
	$(CC) $(CFLAGS) -c at_parse.c -o $@

libat.a: $(OBJS)
	$(AR) rcs $@ $(OBJS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(CLEANFILES)
