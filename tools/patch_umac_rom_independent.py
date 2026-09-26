#!/usr/bin/env python3
"""Patch a user-owned Plus V3 ROM with the MCX 68000 disk stub.

The bytecode below implements the documented Macintosh Device Manager entry
and return conventions. Its copyright provenance requires review before
publication; generating opcode bytes alone does not establish independence
from earlier drivers. Never distribute the output ROM.
"""

import argparse
from pathlib import Path
import struct

SONY_OFFSET = 0x17D30
PV_ADDRESS = 0x00C00069
MEMORY_TEST_BRANCH = 0x132
MEMORY_TOP_INSTRUCTION = 0x376
ORIGINAL_MEMORY_TOP = bytes.fromhex("660643fa002e4ed0")


class Code:
    def __init__(self):
        self.data = bytearray()
        self.labels = {}
        self.fixups = []

    def word(self, value):
        self.data += struct.pack(">H", value & 0xFFFF)

    def long(self, value):
        self.data += struct.pack(">I", value & 0xFFFFFFFF)

    def label(self, name):
        self.labels[name] = len(self.data)

    def branch(self, condition, target):
        self.word(condition)
        self.fixups.append((len(self.data), target))
        self.word(0)

    def pv(self, operation):
        # MOVE.B #operation, ($00C00069).L
        self.word(0x13FC)
        self.word(operation)
        self.long(PV_ADDRESS)

    def finish(self):
        for offset, target in self.fixups:
            # For a 68000 branch.W, PC-relative displacement is based on the
            # address of the extension word (not the following instruction).
            displacement = self.labels[target] - offset
            if not -32768 <= displacement <= 32767:
                raise ValueError("branch displacement out of range")
            struct.pack_into(">h", self.data, offset, displacement)
        return bytes(self.data)


def make_driver(fixed_disk=False):
    code = Code()
    # DRVR header: flags, delay, event mask, menu, five routine offsets.
    for value in (0x6F00, 0, 0, 0):
        code.word(value)
    for routine in ("open", "prime", "control", "status", "close"):
        code.fixups.append((len(code.data), routine))
        code.word(0)
    code.data.extend(b"\x05.Sony")

    code.label("open")
    code.word(0x48E7)  # MOVEM.L A0-A1,-(SP)
    code.word(0x00C0)
    code.word(0x7040 if fixed_disk else 0x7020)  # 64 or 32 status bytes
    code.word(0xA71E)  # NewPtrSysClear
    code.word(0x2448)  # MOVEA.L A0,A2
    code.word(0x4CDF)  # MOVEM.L (SP)+,A0-A1
    code.word(0x0300)
    code.word(0x200A)  # MOVE.L A2,D0 (TST does not accept address registers)
    code.word(0x4A80)  # TST.L D0
    code.branch(0x6700, "open_failed")
    code.pv(0)
    code.word(0x204A)  # MOVEA.L A2,A0
    code.word(0x5C88 if fixed_disk else 0x5888)
    # The fixed-disk experiment also corrects the floppy DrvSts queue
    # pointer to +6 (track word plus four flags). Keep the old stub byte-for-
    # byte stable in normal builds until the dual-drive path is validated.
    code.word(0x203C)  # MOVE.L #($0001 << 16 | (-5 & 0xffff)),D0
    code.long(0x0001FFFB)
    if fixed_disk:
        code.word(0x2F0A)  # MOVE.L A2,-(SP), preserve status base
    code.word(0xA04E)  # AddDrive
    if fixed_disk:
        code.word(0x245F)  # MOVEA.L (SP)+,A2
        code.word(0x45EA)  # LEA 32(A2),A2
        code.word(32)
        code.pv(4)        # initialize the independent fixed disk
        code.word(0x4A40) # TST.W D0; absent disk is not fatal to .Sony
        code.branch(0x6600, "no_fixed_disk")
        code.word(0x204A) # MOVEA.L A2,A0
        code.word(0x5C88) # ADDQ.L #6,A0; DrvSts.track + four flag bytes
        code.word(0x203C) # MOVE.L #(8 << 16 | -5),D0
        code.long(0x0008FFFB)
        code.word(0xA04E) # AddDrive
        code.label("no_fixed_disk")
        code.word(0x7000) # MOVEQ #0,D0
    code.word(0x4E75)
    code.label("open_failed")
    code.word(0x70E9)  # MOVEQ #-23,D0 (openErr)
    code.word(0x4E75)

    code.label("prime")
    code.pv(1)
    code.branch(0x6000, "io_return")
    code.label("control")
    code.pv(2)
    if fixed_disk:
        # C returns +1 only once for an inserted fixed disk. Post a
        # documented diskEvt from the 68000 side, where the Mac OS trap is
        # callable, so SystemEvent will attempt to mount drive 8.
        code.word(0x0C68)  # CMPI.W #65,26(A0): periodic accRun
        code.word(65)
        code.word(26)
        code.branch(0x6600, "control_regular")
        code.word(0x0C40)  # CMPI.W #1,D0: host requests diskEvt
        code.word(1)
        code.branch(0x6600, "control_regular")
        code.word(0x48E7)  # MOVEM.L A0-A1,-(SP)
        code.word(0x00C0)
        code.word(0x7007)  # MOVEQ #diskEvt,D0
        code.word(0x2040)  # MOVEA.L D0,A0
        code.word(0x7008)  # MOVEQ #drive_number,D0
        code.word(0xA02F)  # _PostEvent
        code.word(0x4CDF)  # MOVEM.L (SP)+,A0-A1
        code.word(0x0300)
        code.word(0x7000)  # device control returns noErr
        code.label("control_regular")
    code.word(0x0C68)  # CMPI.W #1,26(A0): KillIO must return immediately
    code.word(1)
    code.word(26)
    code.branch(0x6700, "simple_return")
    code.branch(0x6000, "io_return")
    code.label("status")
    code.pv(3)
    code.branch(0x6000, "io_return")
    code.label("close")
    code.word(0x70E8)  # MOVEQ #-24,D0 (closErr)
    code.word(0x4E75)

    code.label("io_return")
    code.word(0x3228)  # MOVE.W 6(A0),D1: ioTrap
    code.word(6)
    code.word(0x0801)  # BTST #9,D1: immediate request?
    code.word(9)
    code.branch(0x6700, "queued_return")
    code.word(0x3140)  # MOVE.W D0,16(A0): ioResult
    code.word(16)
    code.label("simple_return")
    code.word(0x4E75)
    code.label("queued_return")
    code.word(0x2F38)  # MOVE.L ($08FC).W,-(SP): JIODone
    code.word(0x08FC)
    code.word(0x4E75)

    # Header offsets are absolute within the driver, not branch displacements.
    for offset, routine in code.fixups[:5]:
        struct.pack_into(">H", code.data, offset, code.labels[routine])
    code.fixups = code.fixups[5:]
    return code.finish()


def patch_memory_size(rom, ram_kib):
    """Set the Plus V3 memory top for an intermediate, nonstandard RAM size.

    These two instruction sites are checked against the user's original V3
    ROM before modification. The 68000 code uses A5 as the RAM limit; the
    normal 128/512 KiB test otherwise rejects intermediate sizes.
    """
    if ram_kib not in (128, 192, 208):
        raise ValueError("supported RAM sizes are 128, 192, and 208 KiB")
    if ram_kib == 128:
        return bytes(rom)
    if rom[MEMORY_TEST_BRANCH:MEMORY_TEST_BRANCH + 2] not in (
        bytes.fromhex("6700"), bytes.fromhex("6000")
    ):
        raise ValueError("unexpected memory-test branch in Plus V3 ROM")
    old_instruction = rom[MEMORY_TOP_INSTRUCTION:MEMORY_TOP_INSTRUCTION + 8]
    if old_instruction != ORIGINAL_MEMORY_TOP and not (
        old_instruction[:2] == bytes.fromhex("2a7c")
        and old_instruction[6:] == bytes.fromhex("4e71")
        and int.from_bytes(old_instruction[2:6], "big") in (192 * 1024, 208 * 1024)
    ):
        raise ValueError("unexpected memory-top instruction in Plus V3 ROM")
    output = bytearray(rom)
    output[MEMORY_TEST_BRANCH:MEMORY_TEST_BRANCH + 2] = bytes.fromhex("6000")
    output[MEMORY_TOP_INSTRUCTION:MEMORY_TOP_INSTRUCTION + 8] = (
        bytes.fromhex("2a7c") + struct.pack(">I", ram_kib * 1024)
        + bytes.fromhex("4e71")
    )
    return bytes(output)


def patch_rom(original, ram_kib=128, fixed_disk=False):
    if len(original) != 0x20000 or original[:4] != bytes.fromhex("4d1f8172"):
        raise ValueError("expected an original 128 KiB Mac Plus V3 ROM")
    if original[SONY_OFFSET + 18:SONY_OFFSET + 24] != b"\x05.Sony":
        raise ValueError(".Sony driver header not found at expected location")
    if original[0xD92:0xD94] == bytes.fromhex("b381"):
        raise ValueError("ROM appears already patched; use the original ROM")
    driver = make_driver(fixed_disk)
    if len(driver) > 256:
        raise ValueError("replacement driver exceeds the validated ROM region")
    output = bytearray(original)
    output[0xD92:0xD94] = bytes.fromhex("b381")
    output[SONY_OFFSET:SONY_OFFSET + len(driver)] = driver
    return patch_memory_size(output, ram_kib)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("original_rom", type=Path)
    parser.add_argument("private_output", type=Path)
    parser.add_argument("--ram-kib", type=int, default=128, choices=(128, 192, 208))
    parser.add_argument("--fixed-disk", action="store_true",
                        help="add a second, fixed disk as drive 8")
    args = parser.parse_args()
    if args.private_output.exists():
        parser.error("output already exists; refusing to overwrite")
    rom = patch_rom(args.original_rom.read_bytes(), args.ram_kib,
                    args.fixed_disk)
    with args.private_output.open("xb") as target:
        target.write(rom)
    print("Patched private ROM created; never distribute this file")


if __name__ == "__main__":
    main()
