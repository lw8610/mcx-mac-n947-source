# Publication checklist — 2026-09-27

**Status: the project owner approved source-only publication with the
documented provenance uncertainty on 2026-09-27. Binary distribution is not
approved.** This folder is the curated candidate; the existing private GitHub
repository and the first hardware board are not publication sources.

## Completed

- Selected source files were copied into an independent build tree. Known
  GPLv2-derived uMac floppy, ROM-driver, and keymap files, plus private
  Apple ROM/System assets, were excluded.
- The CMake public-release guard defaults to ON; the legacy floppy and ROM
  patching branches were removed from this candidate.
- The 208 KiB FAT-image configuration builds without the development tree.
- `tools/check_public_release.py` finds no known linked GPL/private-media
  blockers in that build.
- The earlier pre-removal build used 630,292 bytes of internal Flash, 259 KiB of main RAM, and
  71,112 bytes of the 80 KiB application RAM-X region. A compiled-source
  inventory contains 209 sources; the 11 without SPDX tags have been
  classified by inspection in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
- The unused MAME-derived PMMU and SoftFloat 2b/FPU files have now been removed
  from the 68000-only candidate. A fresh asset-free build succeeds using
  628,460 bytes of internal Flash; the release gate passes and no PMMU/FPU/
  SoftFloat symbols appear in the ELF.
- This separate candidate has a single root commit. Its source and reachable
  Git history pass the release gate; the earlier private repository fails the
  history check as intended.
- On 2026-09-27 the exact candidate build was flashed to the second
  FRDM-MCXN947 (probe `1O3C5IXJFOHLW`) with mass erase disabled. The user
  confirmed Finder, the additional disk, and mouse operation. Live serial
  diagnostics showed the guest running, USB connected, and SmartDMA raster
  active. This does not validate a binary release or every user workflow.
- The user observed a short, movable vertical desktop item after opening a
  folder; Close removed it. Its identity and cause remain unconfirmed. Record
  a photo and reproduction steps if it recurs.

## Still required before publishing

1. The project owner confirmed authority to publish original contributions
   under MIT and explicitly accepted the disclosed floppy/ROM-stub provenance
   uncertainty for a **source-only** release on 2026-09-27. See
   [PROVENANCE_REVIEW.md](PROVENANCE_REVIEW.md). This acceptance is not proof
   of independence from GPLv2 source, patent clearance, or legal assurance.
2. Do not change the existing private GitHub repository to public. Its first
   commit retains deleted third-party material. Publish only this separate
   single-commit history in a **new** repository; a later commit deleting
   files from the existing repository is not sufficient.
3. Hardware smoke testing is complete for Finder, disk visibility, and mouse.
   A fresh write/read/reset persistence check of the additional disk with this
   exact build remains desirable before calling it fully regression-tested.
4. Before any binary distribution, replace the USB-transfer test VID/PID,
   validate that mode, create a build-specific third-party notice bundle/SBOM,
   and re-run the release gate on the exact binary. Do not distribute ROM,
   System, apps, or a media package with the firmware.

The automated gate tests known technical blockers only. A pass is not a
copyright, patent, or license clearance.
