# MCX-N947 Macintosh emulator — source release candidate

<img width="8064" height="6048" alt="IMG_2929" src="https://github.com/user-attachments/assets/71b7ff2a-edba-4b6d-8174-db2bf59f3c77" />


https://github.com/user-attachments/assets/aa9723b1-ea45-4c90-969d-0667d9c1c2b9




This is a curated, **source-only** candidate for the FRDM-MCXN947. It runs a
208 KiB Macintosh guest with Zephyr, uMac, Musashi, SmartDMA scanout, USB-host
Boot keyboard/mouse input, and a FAT-hosted Macintosh disk image on external
Flash. USB HID discovery does not depend on a receiver VID/PID, interface
number, or endpoint number. It is
not yet an approved binary release. See [RELEASE_STATUS.md](RELEASE_STATUS.md)
before publishing or distributing anything.

The repository contains **no Apple ROM, System disk, application disk image,
patched ROM, or prebuilt firmware**. Users must provide their own lawfully
obtained Mac Plus v3 ROM and compatible System disk, build a private media
package with `tools/pack_umac_media.py`, and install it separately in image-1.
Do not commit or distribute that package.

The original development tree is not required to build this candidate.
The public build deliberately uses the independently written floppy backend;
the GPLv2-derived uMac floppy, ROM-driver, and keymap files are omitted.
This 68000-only candidate also omits Musashi's unused MAME-derived PMMU and
SoftFloat 2b/FPU material.

## Build

Use a Zephyr 4.4.2 workspace with the FRDM-MCXN947 board and NXP HAL modules.
From this directory, set `ZEPHYR_BASE` for your local installation and run:

```sh
west build -p always -b frdm_mcxn947/mcxn947/cpu0 . -d build_public_fat -- \
  '-DEXTRA_CONF_FILE=umac_boot.conf;umac_usb_mouse.conf;umac_media_slot.conf;umac_independent_floppy.conf;umac_mcxn947_fast.conf;umac_video_dirty.conf;umac_208k.conf;smartdma_guest_descriptor.conf;smartdma_guest_sramx.conf;external_flash_fat_image.conf' \
  '-DDTC_OVERLAY_FILE=boards/frdm_mcxn947_mcxn947_cpu0.overlay;boards/umac_usb_mouse.overlay;experiments/usb_image_transfer/msc_safe_partition.overlay' \
  -DMCX_MAC_PUBLIC_RELEASE=ON
python3 -B tools/check_public_release.py build_public_fat
```

The build also requires the standard Zephyr SDK/host tools. It produces an
asset-free firmware image; it does not produce the private ROM/System media.

The USB-device image-transfer mode used during development is excluded from
the public build. Its test VID/PID is not suitable for a published binary.
Macintosh disk image preparation and copying instructions will be documented
separately when the transfer mode has a production VID/PID and release tests.

## QuickDraw benchmark

`experiments/mac_qd_benchmark` contains source for a small classic Macintosh
application that measures rectangle drawing, screen/offscreen `CopyBits`, and
moving/scaling rectangle animation. It shows the active test while running and
an explicit idle results screen when complete. Build it separately with the
Retro68 68K toolchain; no application binary or disk image is committed here.

## Licenses

The root [LICENSE](LICENSE) covers only original project code. The vendored
uMac and Musashi code retains its own notices. See
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). The ROM-stub provenance
question is recorded in [PROVENANCE_REVIEW.md](PROVENANCE_REVIEW.md); the
automated build/license checks are not legal clearance.
