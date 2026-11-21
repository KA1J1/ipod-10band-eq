# ipod-10band-eq fork

A fork of [osos-lab](https://github.com/lgreitis/osos-lab), the patch set behind
**RepriseOS** for iPod classic. It extends **RepriseOS 0.3.1** (osos-lab tag
`v0.3.1`, commit `8fc840d`) and replaces the stock five-band Custom EQ
with a **ten-band parametric EQ**, adds **eight named tune profiles**, and adds an
**on-device profile editor** — all running inside the patched Apple 2.0.5 firmware.

Everything is built from *your own* firmware image. No Apple firmware, IPSW, or
built `.bin` is included or redistributed.

## Before installing

1. **Check the cluster size first.** The firmware writes the FAT volume assuming
   the stock Apple layout: 4096-byte sectors with **16 KB clusters**. On Windows,
   `chkdsk I:` reports `16,384 bytes in each allocation unit`. If it reports
   65,536 (64 KB), the volume is wrong: Windows' own `format` only offers 64 KB
   clusters above 32 GB, and with them the firmware creates files that have a
   directory entry but no FAT chain (`chkdsk` reports "size not valid"). Equaliser
   settings then fail to save, and the stock system may loop on
   "Do not disconnect" / ejecting.
2. **Rebuild the volume if it is not 16 KB.** Restore the iPod with Apple Devices
   (this rewrites the firmware and rebuilds the file system with the stock
   layout), or format with `fat32format -c 16`. Do not use `format.com`.
3. **Install RepriseOS before this fork.** This repository only builds
   `/osos-cfw.bin`, the patched OS image; it does not contain the bootloader or
   an installer. Get **RepriseOS Installer** from
   [repriseos.com/download](https://repriseos.com/download/) or the
   [installer releases](https://github.com/lgreitis/repriseos-installer/releases)
   and let it install the stock Apple 2.0.5 + RepriseOS 0.3.1 base first. A full
   restore removes the bootloader again, so run the installer after any restore.
   If the installer only offers a newer base than 0.3.1, install it and keep a
   copy of the `osos-cfw.bin` it writes; if this fork's image does not boot,
   restore that copy from your backup before trying anything else.
4. **Then replace only `/osos-cfw.bin`.** Build this fork from your own
   `iPod_38.2.0.5.ipsw` following the upstream build steps, and copy the resulting
   `osos-cfw.bin` over `/osos-cfw.bin` on the iPod (keeping a copy of the
   original). The bootloader and `/cfw-loader.bin` stay as the installer left
   them. Optionally copy a prepared `CFWEQ` to
   `iPod_Control/Device/CFWEQ` for ready-made profiles.

## Firmware and devices

- **Target firmware:** Apple **2.0.5** (OSOS) for iPod classic — the same base
  upstream builds against. This fork is based on osos-lab `v0.3.1` (`8fc840d`) and
  does not change how the image is patched or loaded.
- **Supported models:** iPod classic **6G (80/160 GB)**, **6.5G (120 GB)** and
  **7G (160 GB)** — the 2007–2009 classic models. Each device keeps its own
  identity in its companion image.
- **Tested on:** iPod classic 6.5G 120 GB (model MB562).
- **Not supported:** iPod video 5G/5.5G, nano, shuffle, touch, iPhone or iPad.
  This is iPod classic firmware and cannot be installed on an iPhone.

## What this fork adds

- **Ten-band Custom EQ** — `CFW_EQ_BANDS` 5 → 10, 35 selectable frequencies,
  Q 0.3–6.0, gain in 0.1 dB steps, precut 0 to −10 dB. The same RBJ cookbook
  biquads run at 44.1 and 48 kHz with Q24 fixed-point coefficients, so the extra
  bands cost CPU only when they are enabled.
- **Eight tune slots** — one 1,524-byte settings record (`CFWEQ`) holds eight
  complete tunes plus their names. Switching reuses the existing audio
  crossfade, and the record is written right away; when that write succeeds the
  player resumes the same slot after a reboot.
- **Named profiles** — each slot carries a 24-byte name field. `Custom EQ →
  Edit name` opens a custom-drawn page with one line of ten character cells:
  the wheel changes the letter under the cursor, the centre button advances the
  cursor and Menu saves. The editor covers ten ASCII cells; longer names written
  by other tools are reduced to those cells when the page is opened.
- **Native profile picker** — `Profile` is an ordinary selector row, so the active
  slot is marked with the system checkmark and can be chosen directly instead of
  cycling through the list.
- **Settings migration** — three-band and five-band records (v1–v4) are imported
  into slot 1 on first boot; a v5 eight-slot record keeps its slots and active
  slot; v6 is read directly. The whole record is validated before anything is
  copied, and a malformed record leaves the defaults in place without touching
  the file.
- **No settings write during startup** — the first boot after an upgrade keeps
  the imported tune in memory and stores it on the next edit, slot switch or
  explicit save, so startup only reads the settings file.

## Tools

`tools/cfweq/` holds the PC-side helpers used while developing this fork:
`cfweq_tool.py` reads, verifies, migrates and renames `CFWEQ` files (v1-v6) and
generates ready-made tunes; `audit_curve.py` checks that no intermediate EQ stage
clips; `eq_curve.py` plots a tune exactly as the firmware computes it;
`build_payload.py`, `assemble_recipe.py` and `host_contracts.py` cover building
and the host tests; `install_10band.ps1` and `write_device_window.ps1` write the
payload to a mounted iPod (with backup, readback verification and rollback).
See `tools/cfweq/README.md` for requirements and examples.

## Settings format

```
v1/v2   64 B    3 bands
v3      96 B    5 bands
v4     176 B   10 bands
v5   1,332 B   10 bands × 8 slots
v6   1,524 B   10 bands × 8 slots + 24-byte names      (current)
```

The checksum is FNV-1a over everything but the final word. The file lives at
`iPod_Control/Device/CFWEQ`.

## Requirements and caveats

- One of the supported iPod classic models above, already running Apple 2.0.5
  firmware, with FAT32 formatted at 16 KB clusters (the stock Apple layout).
  Larger clusters make the firmware's own write path produce corrupt files.
- Builds use the same toolchain, recipe and install steps as upstream; follow
  the upstream documentation. (The toolchain prefix differs between upstream
  releases, so check the build script of the release you base your fork on.)
- **The EQ is only audible when the native preset is `Custom`.** Flat/Off bypass
  the chain entirely.
- Flashing patched firmware always carries risk. Keep a copy of your original
  `osos-cfw.bin` and know how to fall back to the stock system.
- Downgrading to firmware without this fork loses the extra slots: older versions
  cannot read the v6 record and fall back to defaults.
- Ten enabled bands roughly double the filter work compared with five; measure
  battery life on your own device.

## Verification

The branch was developed against an iPod classic (6.5G, 120 GB) running
RepriseOS 0.3.1 and exercised on hardware: ten-band editing with save-after-edit,
slot switching that survives a reboot, and on-device renaming. Host-side
contract fixtures and unit tests cover record migration, slot switching, name
normalisation, validation failures and the audio path.

## License

GPL-3.0-only, same as upstream. The full licence text is in `COPYING`. This is
an unofficial fork; please report fork-specific issues here rather than upstream.

## Credits

Upstream: [lgreitis/osos-lab](https://github.com/lgreitis/osos-lab) and its
contributors.

## Upstream

Everything not listed here comes from
[osos-lab](https://github.com/lgreitis/osos-lab); follow its README for the build
and install steps. The change list with commit hashes is in `NOTICE.md`.
