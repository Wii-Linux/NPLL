#
# NPLL - Makefile
#
# Copyright (C) 2025 Techflash
#

ifneq ($(uname -m),ppc)
CROSS_PREFIX ?= $(word 1, \
	$(if $(shell which powerpc-unknown-linux-gnu-gcc),powerpc-unknown-linux-gnu-) \
	$(if $(shell which powerpc-linux-gnu-gcc),powerpc-linux-gnu-) \
	)
ifeq ($(CROSS_PREFIX),)
$(warning WARNING using host CC=$(CC) AR=$(AR), you can set the powerpc prefix with CROSS_PREFIX)
else
$(info CROSS_PREFIX=$(CROSS_PREFIX))
CC := $(CROSS_PREFIX)gcc
AR := $(CROSS_PREFIX)ar
endif
endif

ELF2DOL ?= elf2dol
ifeq ($(shell which $(ELF2DOL)),)
$(warning WARNING $(ELF2DOL) not found, you can get elf2dol from devkitPro gamecube-tools)
endif

ifeq ($(VERBOSE),1)
HIDE :=
else
HIDE := @
endif

ASFLAGS :=
CFLAGS  := -mregnames -mcpu=750 -Iinclude -ggdb3 -nostdinc -ffreestanding -fno-jump-tables -fno-omit-frame-pointer -fstack-protector-strong
#CFLAGS  += -DDO_TRACE
CFLAGS  += -O3 -Wall -Wextra -Wformat=2
LDFLAGS := -nostdlib -nostartfiles -T src/linkerscript.ld

SOURCE  := entry.S gamecube/init.c wii/init.c wii/ios_ipc.c wii/ioshax.c wiiu/init.c
SOURCE  += allocator.c timer.c panic.c init.c drivers.c output.c main.c menu.c video.c input.c elf.c elf_asm.S memlog.c platOps_debug.c tiny_usbgecko.c exception.c exception_2200.S
SOURCE  += libc/printf.c libc/output.c libc/string.c libc/string_asm.S libc/cc-runtime.c stack_protector.c font.c
SOURCE  += drivers/hollywood_gpio.c drivers/exi.c drivers/usbgecko.c drivers/vi.c drivers/latte_framebuffer.c drivers/drc_ipc_text.c drivers/hollywood_sdhc.c drivers/hollywood_sdmmc.c

OBJ     := $(patsubst %.S,build/%.o,$(patsubst %.c,build/%.o,$(SOURCE)))
OUT_ELF := bin/npll.elf
OUT_DOL := bin/npll.dol

.PHONY: all clean

all: $(OUT_ELF) $(OUT_DOL)

$(OUT_DOL): $(OUT_ELF)
	$(HIDE)$(ELF2DOL) $< $@
	$(info $s  ELF2DOL $@)

$(OUT_ELF): $(OBJ)
	$(HIDE)mkdir -p $(@D)
	$(HIDE)$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^
	$(info $s  LD $@)

build/%.o: src/%.c
	$(HIDE)mkdir -p $(@D)
	$(HIDE)$(CC) $(CFLAGS) -o $@ -c $<
	$(info $s  CC $<)

build/%.o: src/%.S
	$(HIDE)mkdir -p $(@D)
	$(HIDE)$(CC) $(ASFLAGS) $(CFLAGS) -o $@ -c $<
	$(info $s  AS $<)


clean:
	rm -rf bin build
