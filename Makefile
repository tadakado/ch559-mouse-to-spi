# CH559 USB mouse host firmware (project-specific build).
# Reuses the atc1441 peripherals (util/uart/USBHost/CH559.h) copied into src/,
# plus the project additions hid_parser.c and spi_out.c.
# wchisp flashes the .hex directly; hex2bin/objcopy are not needed.
# All build artifacts are written under build/ to keep the top folder clean.

PROJECT   = ch559mouse
FREQ_SYS  = 48000000
XRAM_SIZE = 0x0800
XRAM_LOC  = 0x0600
CODE_SIZE = 0xEFFF

CC      = sdcc
PACKIHX = packihx
WCHISP  = wchisp
SRCDIR  = src
BUILD   = build

CFLAGS  = -mmcs51 --model-large \
          --xram-size $(XRAM_SIZE) --xram-loc $(XRAM_LOC) \
          --code-size $(CODE_SIZE) \
          -I$(SRCDIR) -DFREQ_SYS=$(FREQ_SYS)

# UART debug output (Startup/Ready/Mouse/per-report "M ..." line + enumeration
# frames) is ON by default. `make RELEASE=1` compiles it all out for production
# (the per-report printf otherwise blocks the main loop ~2-3 ms at 115200 baud).
ifndef RELEASE
CFLAGS += -DDEBUG_UART
endif

# `make RAW=1` additionally dumps the raw interrupt-IN report hex on UART0
# (implies the UART debug output). Toggling any of these requires `make clean`
# first (object files are cached).
ifdef RAW
CFLAGS += -DDEBUG_RAW -DDEBUG_UART
endif

SRCS = main.c util.c uart.c USBHost.c hid_parser.c spi_out.c
RELS = $(addprefix $(BUILD)/,$(SRCS:.c=.rel))
HEX  = $(BUILD)/$(PROJECT).hex

# Host unit-test build for the hardware-independent parser.
HOSTCC    = cc
HOSTFLAGS = -DHOST_TEST -I$(SRCDIR) -Wall -Wextra

.PHONY: all flash test clean

all: $(HEX)

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/%.rel: $(SRCDIR)/%.c | $(BUILD)
	$(CC) -c $(CFLAGS) $< -o $@

$(BUILD)/$(PROJECT).ihx: $(RELS)
	$(CC) $(CFLAGS) $(RELS) -o $@

$(HEX): $(BUILD)/$(PROJECT).ihx
	$(PACKIHX) $< > $@

flash: $(HEX)
	$(WCHISP) flash $(HEX)

test: | $(BUILD)
	$(HOSTCC) $(HOSTFLAGS) test/test_hid_parser.c $(SRCDIR)/hid_parser.c -o $(BUILD)/test_hid
	./$(BUILD)/test_hid

clean:
	rm -rf $(BUILD)
