# Floppy and ROM-stub provenance review

This is a technical comparison, not a legal opinion or a clean-room
certification. It records the uncertainty that remains when publishing this
source. The root MIT license applies only to original project code.

## Material examined

- MCX `src/emulator/umac_floppy.c` and
  `tools/patch_umac_rom_independent.py` in this candidate.
- uMac upstream commit `b62d3c6e725ca2ac2224e710b3cee765dbc35e45`:
  `src/disc.c` and `macsrc/sonydrv.S`. The upstream README identifies these
  as based on Basilisk II and GPLv2-derived. They are not in this candidate.
- Apple's [Inside Macintosh: Device Manager](https://developer.apple.com/library/archive/documentation/mac/pdf/Devices/Device_Manager.pdf)
  describes the Device Manager interface, including the required driver
  entry points and system calls. It is an interface reference, not a license
  to copy another driver's implementation.

## Findings

- The MCX C driver has different storage handling, bounds checks, a second
  fixed disk, and a different layout from uMac's host-side `disc.c`. A source
  inspection and whitespace-normalized line comparison did not find a copied
  substantive block of C text (the few three-line matches are braces,
  blank lines, and `return 0`). Both must implement the
  same Macintosh Device Manager interface, so shared offsets, error values,
  entry points, and control codes alone do not establish copying.
- The MCX ROM generator emits a `.Sony` driver header and the same broad
  Open/Prime/Control/Status/Close flow as `sonydrv.S`. Its Open and I/O-return
  paths also use some of the same 68000 instruction sequences, including
  saving A0/A1, calling `NewPtrSysClear`, calling `AddDrive`, and returning
  through the I/O-completion vector. The five entry points and parameter
  conventions are documented by Apple. Apple's Device Manager chapter even
  shows the `ioTrap` test and `MOVE.L JIODone,-(SP)` / `RTS` return sequence
  as an example. This explains part of the overlap as a conventional way to
  implement the public interface. It does not establish how this particular
  implementation was authored. The available development history does not
  establish that all similarities came solely from public interface
  documentation.
- Generating machine code in Python instead of including the upstream
  assembly does not by itself remove a possible derivative-work issue.
- The candidate does not include upstream `keymap.h` or `keymap_sdl.h`.
  `umac_serial_input.c` receives numeric Mac key codes over UART and does not
  contain the old key-name translation table.

## Release decision

The asset-free source build and license gate may pass, but they do not settle
the ROM-stub provenance. A cosmetic opcode reorder would not settle it either.
The project owner explicitly chose source-only publication with this
uncertainty disclosed on 2026-09-27. This does not provide a guarantee against
a claim. Seeking rightsholder permission or commissioning a truly independent
implementation based only on public interface specifications remain possible
ways to reduce the uncertainty later. Do not publish a binary with Apple
ROM/System media. Keep the existing GitHub repository private: its first
commit retains files removed from the current tree.
