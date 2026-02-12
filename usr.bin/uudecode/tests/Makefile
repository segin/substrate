PROG = test_uudecode_host
SRCS = ../uudecode.c ../../../usr.lib/libuu/uudecode.c

all: $(PROG)

$(PROG): $(SRCS)
	cc -g -Wall -I../../../usr.lib/libuu -I../../../sys -o $@ $^

clean:
	rm -f $(PROG) *.out test.uu test_path.uu dangerous.txt specific.txt test_output.txt

test: $(PROG)
	chmod +x test_cli.sh
	./test_cli.sh ./$(PROG)
