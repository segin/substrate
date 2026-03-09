PYTHON = python3
AS = ia16-elf-as
LD = ia16-elf-ld
OBJCOPY = ia16-elf-objcopy

ELKS_BUILD = $(CURDIR)/build
MKELKS = $(CURDIR)/mkelks.py

PROGRAMS = hello_elks sleep_elks fileio_elks fork_elks

.PHONY: all clean

all: $(PROGRAMS)

$(ELKS_BUILD):
	mkdir -p $(ELKS_BUILD)

$(ELKS_BUILD)/%.o: %.S | $(ELKS_BUILD)
	$(AS) -o $@ $<

$(ELKS_BUILD)/%.elf: $(ELKS_BUILD)/%.o | $(ELKS_BUILD)
	$(LD) -melf_i386 -Ttext 0x0 -e _start -nostdlib -o $@ $<

$(ELKS_BUILD)/%.bin: $(ELKS_BUILD)/%.elf | $(ELKS_BUILD)
	$(OBJCOPY) -O binary $< $@

%_elks: $(ELKS_BUILD)/%.bin $(MKELKS)
	$(PYTHON) $(MKELKS) --text $< --output $@ --stack 4096

clean:
	rm -rf $(ELKS_BUILD) $(PROGRAMS)
