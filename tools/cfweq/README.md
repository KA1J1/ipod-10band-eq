# cfweq tools

Helpers for the ten-band Custom EQ in this fork: reading and writing the
`CFWEQ` settings file, checking headroom, building the payload, and writing it
to a mounted iPod.

These tools are convenience wrappers around the same tables and formats the
firmware uses (`payload/custom_eq.c`, `payload/eq_coefficients.c`). The upstream
build and install flow in the repository root remains authoritative; nothing
here touches the bootloader, the companion, themes or the music library.

## Requirements

| Tool | Needs |
|---|---|
| `cfweq_tool.py`, `audit_curve.py` | Python 3.10+ (standard library only) |
| `eq_curve.py` | Python 3.10+ and `matplotlib` (only for `--out`) |
| `host_contracts.py` | a host C compiler (`gcc`, `cc` or MinGW) |
| `build_payload.py` | an ARM `arm-none-eabi` GCC toolchain |
| `assemble_recipe.py` | Python only, plus the firmware inputs and the recipe JSON |
| `install_10band.ps1`, `write_device_window.ps1` | PowerShell 5.1+ / 7 on Windows |

On Windows, `host_contracts.py` writes temporary files; if the default temp
directory is locked down, point `TMP` and `TEMP` at a writable folder first.

## Tools

| File | Purpose |
|---|---|
| `cfweq_tool.py` | Read, verify, migrate, rename and create `CFWEQ` files (v1–v6). |
| `audit_curve.py` | Per-stage cumulative peak of a tune: proves no intermediate stage clips. |
| `eq_curve.py` | Response curve of a tune, computed exactly like the firmware; optional PNG. |
| `build_payload.py` | Compiles the payload with the ARM toolchain and writes the recipe/data. |
| `assemble_recipe.py` | Applies a recipe to the inputs and produces `osos-cfw.bin` (no Rust needed). |
| `host_contracts.py` | Compiles and runs the C contract fixtures on the host. |
| `install_10band.ps1` | Backup, write, read back and verify `/osos-cfw.bin`; `-Rollback` restores. |
| `write_device_window.ps1` | Same write, but only inside a stable USB window and via a staged `.new` file. |

`CFWEQ` lives at `iPod_Control\Device\CFWEQ` on the iPod. The file stores
selector indices, not physical values, so always use these tools (or the
on-device editor) rather than hand-editing it.

## Examples

Verify what the device currently holds and list all eight slots (the active one
is marked with `*`):

```powershell
python cfweq_tool.py verify I:\iPod_Control\Device\CFWEQ --slots
```

Rename a profile (the editor on the device shows at most ten characters):

```powershell
python cfweq_tool.py rename I:\iPod_Control\Device\CFWEQ --slot 2 --name "MY PROFILE"
```

Create a fresh file with eight empty slots and their default names, then copy it
to the device (this is also what a factory-fresh settings file looks like):

```powershell
python cfweq_tool.py defaults build/CFWEQ.empty
Copy-Item build/CFWEQ.empty I:\iPod_Control\Device\CFWEQ
```

The tools ship no pre-made tunes: the firmware's own defaults are flat (0 dB
everywhere), and the on-device or `rename` path gives each slot a name.

Check that a tune never clips in any intermediate stage, and plot it:

```powershell
python audit_curve.py build/CFWEQ.empty
python eq_curve.py --file build/CFWEQ.empty --out build/eq-curve.png
```

Build the payload from a checkout and assemble the image:

```powershell
python build_payload.py --repo . --out build/release `
    --prefix C:\tools\arm-none-eabi\bin\arm-none-eabi-
python assemble_recipe.py --recipe build/release/osos.json `
    --data build/release/osos.data --inputs inputs --out build/release/osos-cfw.bin
```

Run the host contract tests (the same fixtures the repository's test suite uses):

```powershell
python host_contracts.py --repo .
```

Install the built image, or roll back the last one:

```powershell
powershell -ExecutionPolicy Bypass -File install_10band.ps1 -Drive I: -Image build\release\osos-cfw.bin
powershell -ExecutionPolicy Bypass -File install_10band.ps1 -Drive I: -Rollback
```

If the volume keeps dropping during enumeration, stage the write instead:

```powershell
powershell -ExecutionPolicy Bypass -File write_device_window.ps1 -Drive I: -Image build\release\osos-cfw.bin
```

## Safety notes

- `install_10band.ps1` backs up whatever `/osos-cfw.bin` is already on the
  device to `backup/` next to the script before writing, and verifies the
  readback hash. Keep that backup; it is the easiest way back to a working boot.
- Only `/osos-cfw.bin` (and `CFWEQ` with `-EqFile`) is written. A full restore
  with Apple Devices removes the RepriseOS bootloader again, so run the
  RepriseOS installer before writing this payload after a restore.
- Make sure the volume uses the stock 16 KB clusters before writing anything;
  see the "Before installing" section in `FORK.md`.
- Eject the iPod with the operating system's safe-remove dialog. Pulling the
  cable while a write is in flight is the usual way to damage the FAT volume.
