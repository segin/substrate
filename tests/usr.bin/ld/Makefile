.PHONY: all clean run

all: run

# The scripts build the linker for the host in directories of their own.
run:
	./test_script.sh
	./test_archive.sh
	./test_dynamic.sh
	./test_output.sh
	./test_tls.sh
	./test_got.sh

clean:
