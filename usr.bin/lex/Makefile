PROG=   lex
SRCS=   main.c options.c parser.c symtab.c regex.c dfa.c codegen.c
OBJS=   $(SRCS:.c=.o)
CFLAGS= -Wall -Wextra -Werror -I. -g
BINDIR= /usr/bin

all: $(PROG)

$(PROG): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

clean:
	rm -f $(PROG) $(OBJS)

.PHONY: all clean
