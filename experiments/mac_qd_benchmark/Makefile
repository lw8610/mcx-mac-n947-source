# Retro68 toolchain root, overridable by the caller.
RETRO68 ?= ../../../Retro68-build/toolchain
PREFIX := $(RETRO68)/m68k-apple-macos
CC := $(RETRO68)/bin/m68k-apple-macos-gcc
REZ := $(RETRO68)/bin/Rez
RINCLUDES := $(PREFIX)/RIncludes

CFLAGS := -Os -Wall -Wextra
REZFLAGS := -I$(RINCLUDES)

all: MCXQDBench.bin MCXQDBench.APPL MCXQDBench.dsk

qdbench.o: qdbench.c
	$(CC) $(CFLAGS) -c $< -o $@

MCXQDBench.code.bin: qdbench.o
	$(CC) $< -o $@

MCXQDBench.bin MCXQDBench.APPL MCXQDBench.dsk: MCXQDBench.code.bin
	$(REZ) $(REZFLAGS) --copy MCXQDBench.code.bin \
		$(RINCLUDES)/Retro68APPL.r -t APPL -c MCXB \
		-o MCXQDBench.bin --cc MCXQDBench.APPL --cc MCXQDBench.dsk

clean:
	rm -f qdbench.o MCXQDBench.code.bin MCXQDBench.code.bin.gdb \
		MCXQDBench.bin MCXQDBench.APPL MCXQDBench.dsk

.PHONY: all clean
