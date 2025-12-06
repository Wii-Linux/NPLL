#
# NPLL - GeckoLoad - Makefile
#
# Copyright (C) 2025 Techflash
#

CFLAGS  := -Iinclude -ggdb3 -fstack-protector-strong
CFLAGS  += -O3 -Wall -Wextra -Wformat=2

SOURCE  := gecko.c main.c

OBJ     := $(patsubst %.S,build/%.o,$(patsubst %.c,build/%.o,$(SOURCE)))
OUT     := bin/geckoload

.PHONY: all clean

all: $(OUT)

$(OUT): $(OBJ)
	@mkdir -p $(@D)
	@$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^
	$(info $s  LD $@)

build/%.o: src/%.c
	@mkdir -p $(@D)
	@$(CC) $(CFLAGS) -o $@ -c $<
	$(info $s  CC $<)

clean:
	rm -rf bin build
