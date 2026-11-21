# Changes in this fork

Unofficial fork of [osos-lab](https://github.com/lgreitis/osos-lab). Based on
release tag `v0.3.1` (`8fc840d`). Everything below is contained in the branch
`feat/10band-eq`; the upstream project remains GPL-3.0-only and so is this fork.

## Final state

Modified 2026-11-09 through 2026-11-21. The Custom EQ screen holds ten bands and
eight named tune profiles; the active profile is chosen from a native selector
and renamed on the device from a custom-drawn page. Files touched:

```
payload/custom_eq.c        payload/custom_eq.ui      payload/eq_canvas.c
payload/eq_coefficients.c  payload/eq_coefficients.h payload/custom_eq.h
payload/eq_audio.c         payload/ui.c              payload/ui.h
patching/ui/compiler.py    patching/tests/test_ui.py
patching/tests/test_eq_coefficients.py
patching/tests/fixtures/eq_preferences.c
patching/tests/fixtures/eq_canvas.c
```

The full licence text stays in `COPYING`; every source file keeps its original
SPDX identifier and copyright notice.

## Changes

- Ten-band Custom EQ: `CFW_EQ_BANDS` 5 → 10, a ten-stage biquad chain per
  channel, 35 selectable frequencies, 0.1 dB gain steps and a Q limit of 6.0.
  The settings record becomes 176-byte version 4.
- Eight stored tunes in one record: version 5 (1,332 bytes) with a profile
  picker; version 6 (1,524 bytes) adds a name field per slot.
- Profile names are shown through a small string-resolver hook instead of a
  static label table, so the picker can list user names and mark the active
  slot with the native checkmark.
- On-device renaming: a custom-drawn page with one line of ten character cells
  (wheel changes the letter under the cursor, centre moves the cursor, Menu
  saves). The first iteration used ten selector rows; it was replaced by the
  canvas page so the whole name is edited in one place.
- Import of older records: three- and five-band records (v1–v4) land in slot 1;
  a v5 record keeps its slots; v6 is read directly. A record is validated in full
  before any slot is copied.
- Startup no longer writes the settings file: an imported record stays dirty in
  memory and is stored on the next edit, slot switch or explicit save. The
  version 5 record used during import was moved from the stack to static memory.
- Fixes found while testing and reviewing: a third label for the save row
  (write failure vs rejected filter), bounds checks on imported precut values, an
  exact slot-count check, and separate controller callbacks for the name page so
  it cannot share state with the EQ canvas.
- `tools/cfweq/`: the settings-file tool (verify, migrate, defaults, rename),
  curve audit and plotter, build/assemble wrappers, host contract runner and the
  two device write scripts. The tools ship no pre-made tunes.

## Notes

- No Apple firmware, IPSW, or built binary is part of this repository. The build
  consumes the user's own 2.0.5 image, exactly like upstream.
- These changes add no device serial numbers, host paths or personal
  identifiers; the diff was checked for them. `firmware/bitmaps.json` is inherited
  unchanged from upstream and contains only resource names, offsets and sizes,
  not bitmap data.
- The settings format is forward-only in practice: this fork reads v1–v6, while
  older firmware cannot read v5/v6 and falls back to its defaults.
