#
# NPLL - Makefile
#
# Copyright (C) 2025-2026 Techflash
#

ifeq ($(HOSTCC),)
HOSTCC := $(CC)
endif

ifneq ($(uname -m),ppc)
CROSS_PREFIX ?= $(word 1, \
	$(if $(shell command -v powerpc-unknown-linux-gnu-gcc),powerpc-unknown-linux-gnu-) \
	$(if $(shell command -v powerpc-linux-gnu-gcc),powerpc-linux-gnu-) \
	)
ifeq ($(CROSS_PREFIX),)
$(warning WARNING: Unable to autodetect PowerPC cross-toolchain, Using host CC=$(CC) AR=$(AR), you can set the PowerPC cross-toolchain prefix with CROSS_PREFIX)
else
CC := $(CROSS_PREFIX)gcc
AR := $(CROSS_PREFIX)ar
endif
endif

# for building MINI
# the basename dance is a hack, since MINI needs a very specific value of WIIDEV.
# If the path to your cross compiler looks like:
# /path/to/whatever/armeb-eabi-gcc/bin/armeb-eabi-gcc
# Then MINI needs WIIDEV=/path/to/whatever/armeb-eabi-gcc
# List of toolchain prefixes to try (in order of preference)
ARM_TOOLCHAIN_PREFIXES := armeb-eabi- arm-none-eabi-

define find_arm_toolchain
$(foreach prefix,$(ARM_TOOLCHAIN_PREFIXES), \
	$(if $(shell command -v $(prefix)gcc), \
		$(prefix)))
endef

ARM_TOOLCHAIN_PREFIX ?= $(word 1,$(strip $(find_arm_toolchain)))

ifeq ($(ARM_TOOLCHAIN_PREFIX),)
ifneq ($(DEVKITARM),)
# use devkitPro devkitarm-gcc
ARM_TOOLCHAIN_PREFIX := arm-none-eabi-
WIIDEV := $(DEVKITARM)
else
$(error FATAL: Unable to autodetect ARM Big-Endian EABI cross-toolchain for building MINI.  Please set the ARM_TOOLCHAIN_PREFIX environment variable or provide one of the following in your PATH: $(foreach prefix,$(ARM_TOOLCHAIN_PREFIXES),$(prefix)gcc ))
endif
endif

WIIDEV ?= $(shell dirname "$$(dirname "$$(command -v $(ARM_TOOLCHAIN_PREFIX)gcc)")")
ifeq ($(WIIDEV),)
$(error FATAL: Unable to determine WIIDEV path from $(ARM_TOOLCHAIN_PREFIX)gcc)
endif

export WIIDEV
export ARM_TOOLCHAIN_PREFIX

ELF2DOL ?= elf2dol
ifeq ($(shell command -v $(ELF2DOL)),)
$(warning WARNING $(ELF2DOL) not found, you can get elf2dol from devkitPro gamecube-tools, will skip building DOL)
BUILD_DOL := 0
endif

ifeq ($(VERBOSE),1)
HIDE :=
else
HIDE := @
endif

HOSTCFLAGS := -O3 -Wall -Wextra -Wformat=2

ASFLAGS :=
CFLAGS  := -mregnames -mcpu=750 -Iinclude -ggdb3 -nostdinc -ffreestanding -fno-jump-tables -fno-omit-frame-pointer -fstack-protector-strong
#CFLAGS  += -DDO_TRACE
CFLAGS  += -O3 -Wall -Wextra -Wformat=2
LDFLAGS := -nostdlib -nostartfiles -T src/linkerscript.ld

SOURCE  := entry.S gamecube/init.c wii/init.c wii/ios_ipc.c wii/ios_es.c wii/ioshax.c wiiu/init.c
SOURCE  += allocator.c timer.c panic.c init.c drivers.c output.c main.c menu.c video.c input.c elf.c elf_asm.S memlog.c platOps_debug.c tiny_usbgecko.c exception.c exception_2200.S
SOURCE  += libc/printf.c libc/output.c libc/string.c libc/string_asm.S libc/cc-runtime.c stack_protector.c font.c
SOURCE  += drivers/hollywood_gpio.c drivers/exi.c drivers/usbgecko.c drivers/vi.c drivers/latte_framebuffer.c drivers/drc_ipc_text.c drivers/hollywood_sdhc.c drivers/hollywood_sdmmc.c
SOURCE  += armboot_bin.c

OBJ     := $(patsubst %.S,build/%.o,$(patsubst %.c,build/%.o,$(SOURCE)))
OUT_ELF := bin/npll.elf
OUT_DOL := bin/npll.dol

.PHONY: all clean

ifeq ($(BUILD_DOL),0)
all: $(OUT_ELF)
else
all: $(OUT_ELF) $(OUT_DOL)
endif

$(OUT_DOL): $(OUT_ELF)
	$(info $s  ELF2DOL $@)
	$(HIDE)$(ELF2DOL) $< $@

$(OUT_ELF): $(OBJ)
	$(info $s  LD $@)
	$(HIDE)mkdir -p $(@D)
	$(HIDE)$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^

build/%.o: src/%.c
	$(info $s  CC $<)
	$(HIDE)mkdir -p $(@D)
	$(HIDE)$(CC) $(CFLAGS) -o $@ -c $<

build/%.o: src/%.S
	$(info $s  AS $<)
	$(HIDE)mkdir -p $(@D)
	$(HIDE)$(CC) $(ASFLAGS) $(CFLAGS) -o $@ -c $<

# to make it bail if the user doesn't have MINI pulled
external/mini/armboot.bin: external/mini/Makefile
	$(HIDE)$(MAKE) -C external/mini

src/armboot_bin.c: util/bin2c external/mini/armboot.bin
	$(info $s  BIN2C $@)
	$(HIDE)util/bin2c

# not quite true but close enough
src/armboot_bin.h: src/armboot_bin.c
src/wii/init.c: src/armboot_bin.h

util/bin2c: util/bin2c.c
	$(HIDE)mkdir -p $(@D)
	$(info $s  HOSTCC    $<)
	$(HIDE)$(HOSTCC) $(HOSTCFLAGS) $< -o $@

clean:
	rm -rf bin build utils/bin2c
	$(MAKE) -C external/mini clean
