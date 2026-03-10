PYTHON = python3
CC = ia16-elf-gcc
AS = ia16-elf-as
LD = ia16-elf-ld
OBJCOPY = ia16-elf-objcopy

CFLAGS = -Os -ffreestanding -fno-pic -mcmodel=small
LDFLAGS = -melf_i386 -Ttext 0x0 -e _start -nostdlib

ELKS_BUILD = $(CURDIR)/build
MKELKS = $(CURDIR)/mkelks.py

ASM_SOURCES = hello.S sleep.S fileio.S fork.S bounds_test.S
C_SOURCES = cat.c

ASM_PROGRAMS = $(ASM_SOURCES:.S=_elks)
C_PROGRAMS = $(C_SOURCES:.c=_elks)
PROGRAMS = $(ASM_PROGRAMS) $(C_PROGRAMS)

.PHONY: all clean

all: $(PROGRAMS)

$(ELKS_BUILD):
	mkdir -p $(ELKS_BUILD)

$(ELKS_BUILD)/%.o: %.S | $(ELKS_BUILD)
	$(AS) -o $@ $<

$(ELKS_BUILD)/%.o: %.c | $(ELKS_BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

$(ELKS_BUILD)/%.elf: $(ELKS_BUILD)/%.o | $(ELKS_BUILD)
	$(LD) $(LDFLAGS) -o $@ $<

$(ELKS_BUILD)/%.bin: $(ELKS_BUILD)/%.elf | $(ELKS_BUILD)
	$(OBJCOPY) -O binary $< $@

%_elks: $(ELKS_BUILD)/%.bin $(MKELKS)
	$(PYTHON) $(MKELKS) --text $< --output $@ --stack 4096

clean:
	rm -rf $(ELKS_BUILD) $(PROGRAMS)
