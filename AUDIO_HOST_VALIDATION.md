# Host audio conversion — first hardware-independent stage

This stage does not enable I2S or touch the VGA, USB, media, or board setup.
It establishes a portable reference path for Macintosh audio before an audio
module is purchased.

## Implemented

- Input: 22,254.545454... Hz (`244800 / 11`), unsigned 8-bit mono.
- Output: signed 16-bit mono at 48 kHz or 44.1 kHz.
- Exact rate steps: `51 / 110` input samples per 48 kHz output sample and
  `272 / 539` per 44.1 kHz output sample.
- A 31-tap Q15 FIR low-pass stage followed by fractional linear interpolation.
- Streaming state that preserves FIR history and fractional phase across
  arbitrary input block boundaries.
- A finite-stream finish operation for deterministic WAV length.

The FIR coefficients are shared reference data for the later CPU CMSIS-DSP
and MCX-N947 PowerQuad backends. The scalar implementation is the correctness
reference, not a performance result.

## Host test

```sh
cc -std=c11 -Wall -Wextra -Werror -Isrc \
  tests/umac_audio_convert_test.c src/audio/umac_audio_convert.c \
  -o /tmp/umac_audio_convert_test
/tmp/umac_audio_convert_test
```

The test covers unsigned-to-signed conversion, silence, exact output sample
counts for both rates, and byte-for-byte equality when the same source is fed
as one block, 37/113-sample blocks, or one sample at a time.

## Generate listening files

```sh
cc -std=c11 -Wall -Wextra -Werror -Isrc \
  tools/umac_audio_wav.c src/audio/umac_audio_convert.c -lm \
  -o /tmp/umac_audio_wav
/tmp/umac_audio_wav /tmp/umac_audio_48k.wav 48000
/tmp/umac_audio_wav /tmp/umac_audio_44k1.wav 44100
```

The generated source is a 1 kHz tone in Macintosh 8-bit form. Upsampling and
16-bit output make it compatible with common audio hardware; they do not
restore information absent from the original 8-bit signal.

## Next stage

Add a CMSIS-DSP FIR backend with the same coefficients and compare its output
against the scalar reference. After that passes, add the PowerQuad backend and
measure error, processing time, CPU occupancy, and the cost of its synchronous
completion wait. Only measured results should be used in performance claims.
