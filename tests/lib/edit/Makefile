EDIT_DIR = ../../../lib/edit
CFLAGS_HOST = -g -Wall -DHOST_TEST -I../../../sys -I../../../include -I$(EDIT_DIR)

EDIT_SRCS = $(EDIT_DIR)/editline.c $(EDIT_DIR)/readline.c $(EDIT_DIR)/terminal.c \
            $(EDIT_DIR)/history.c $(EDIT_DIR)/signal.c $(EDIT_DIR)/keymap.c \
            $(EDIT_DIR)/tokenizer.c $(EDIT_DIR)/rl_compat.c $(EDIT_DIR)/utf8.c

HOST_TESTS = host_test_history test_editline test_line_buffer test_keymap \
             test_tokenizer test_editrc test_emacs test_vi test_property \
             test_fuzz test_integration

all: $(HOST_TESTS)

host_test_history: host_test_history.c
	$(CC) $(CFLAGS_HOST) host_test_history.c -o $@

test_editline: test_editline.c $(EDIT_SRCS)
	$(CC) $(CFLAGS_HOST) $(EDIT_SRCS) test_editline.c -o $@

test_line_buffer: test_line_buffer.c $(EDIT_SRCS)
	$(CC) $(CFLAGS_HOST) $(EDIT_SRCS) test_line_buffer.c -o $@

test_keymap: test_keymap.c $(EDIT_SRCS)
	$(CC) $(CFLAGS_HOST) $(EDIT_SRCS) test_keymap.c -o $@

test_tokenizer: test_tokenizer.c
	$(CC) $(CFLAGS_HOST) test_tokenizer.c -o $@

test_editrc: test_editrc.c $(EDIT_SRCS)
	$(CC) $(CFLAGS_HOST) $(EDIT_SRCS) test_editrc.c -o $@

test_emacs: test_emacs.c $(EDIT_SRCS)
	$(CC) $(CFLAGS_HOST) $(EDIT_SRCS) test_emacs.c -o $@

test_vi: test_vi.c $(EDIT_SRCS)
	$(CC) $(CFLAGS_HOST) $(EDIT_SRCS) test_vi.c -o $@

test_property: test_property.c $(EDIT_SRCS)
	$(CC) $(CFLAGS_HOST) $(EDIT_SRCS) test_property.c -o $@

test_fuzz: test_fuzz.c
	$(CC) $(CFLAGS_HOST) test_fuzz.c -o $@

test_integration: test_integration.c $(EDIT_SRCS)
	$(CC) $(CFLAGS_HOST) $(EDIT_SRCS) test_integration.c -o $@

check: all
	@for t in $(HOST_TESTS); do echo "=== $$t ==="; ./$$t || exit 1; done
	@echo "All tests passed."

clean:
	rm -f $(HOST_TESTS)
