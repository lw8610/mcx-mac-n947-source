# uMac QuickDraw benchmark results

Use the same application binary and run each configuration three times after
a fresh boot. Enter the median tick count in the comparison table.

## Test conditions

| Field | MCX-N947 | RP2040 |
| --- | --- | --- |
| Board | FRDM-MCXN947 | |
| Firmware commit/build | | |
| CPU clock | | |
| Emulated Mac RAM | | |
| Display mode | | |
| QuickDraw mode | ROM / safe / aggressive | ROM / accelerated |

## Median results (lower ticks is faster)

| Test | Iterations | MCX-N947 ticks | RP2040 ticks | MCX/RP speed ratio |
| --- | ---: | ---: | ---: | ---: |
| FrameRect screen | 10000 | | | |
| PaintRect screen | 2000 | | | |
| CopyBits screen-screen | 10000 | | | |
| CopyBits screen-offscreen | 10000 | | | |
| CopyBits offscreen-offscreen | 10000 | | | |
| CopyBits offscreen-screen | 10000 | | | |

Record any drawing corruption separately from speed. A fast result with visual
errors is not a valid acceleration result.
