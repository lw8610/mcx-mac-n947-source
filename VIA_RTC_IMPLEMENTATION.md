# VIA timers and RTC/PRAM

Implemented: 783,360 Hz VIA timing; Timer 1 one-shot/free-running modes;
Timer 2 interval/PB6 pulse-counting modes; IFR/IER behavior; the RTC serial
interface; a 1904-epoch seconds counter; 256-byte direct/extended PRAM access;
and write protection. Each video frame contributes 370 PB6 horizontal-period
pulses.

The emulator exposes RTC/PRAM getters and a seconds setter. The current board
integration does not yet persist PRAM across a power cycle or seed wall time
from the board RTC. State survives an emulated Macintosh reset and Macintosh
software can read and set it normally.
