# MCX QuickDraw benchmark

This is a deliberately small 68000 Macintosh application for measuring the
same QuickDraw workload on MCX-N947 uMac, RP2040 uMac, and other compatible
68000 Macintosh environments. It reports elapsed 60 Hz ticks and calculated
operations per second for:

- `FrameRect` to the screen
- `PaintRect` to the screen
- screen-to-screen `CopyBits`
- screen-to-offscreen `CopyBits`
- offscreen-to-offscreen `CopyBits`
- offscreen-to-screen `CopyBits`
- moving a rectangle (erase old outline, change position, draw new outline)
- scaling a rectangle (erase old outline, change size, draw new outline)

The window shows the current test while the benchmark is running. After all
tests complete, the results remain visible and the benchmark is idle. Press R
to run it again. Press Command-Q or use the window close box to quit.

The separation matters: only the complete offscreen-to-offscreen case is
eligible in the current correctness-approved accelerator. Screen reads and
writes intentionally fall back to Macintosh ROM QuickDraw.

The application uses `TickCount()` rather than reading low memory address
`0x016A` directly. They expose the same 60 Hz timebase, while the Toolbox call
avoids depending on a torn two-word read of a changing 32-bit value.

Build with the Retro68 68K toolchain:

```sh
make RETRO68=/absolute/path/to/Retro68-build/toolchain
```

Expected outputs are a MacBinary application, an AppleDouble-style host copy,
and a small disk image. Preserve the application resource fork when copying it
to the uMac external disk.

Run each firmware configuration three times after a fresh boot and record the
median ticks. Correctness observations, measured ticks, and perceived speed
must remain separate.

Use the exact same `MCXQDBench.bin` (or `MCXQDBench.dsk`) on MCX-N947 and
RP2040. That keeps the 68000 application code, iteration counts, rectangles,
and timer source identical. For a clean comparison, record the RP2040 board,
clock setting, RAM configuration, display output mode, and firmware commit.
