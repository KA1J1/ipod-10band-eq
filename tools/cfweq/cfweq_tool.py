#!/usr/bin/env python3
"""Read, verify, convert and create RepriseOS Custom EQ files (CFWEQ).

The firmware stores selector *indices*, not physical values, so this tool also
mirrors the selector tables from payload/custom_eq.ui:

  type      0 Off, 1 Peaking, 2 Low shelf, 3 High shelf
  frequency 35 values, 20 Hz .. 20 kHz
  Q         tenths, 3 .. 60 (0.3 .. 6.0)
  gain      tenths, -120 .. 120 (-12.0 .. +12.0 dB)
  precut    tenths, 0 .. -100 (index n is -n tenths)

File layout, all words little endian:

  v1, v2   13 fields (three bands)   magic + version + 13 * u32 + checksum =  64 B
  v3       21 fields (five bands)    magic + version + 21 * u32 + checksum =  96 B
  v4       41 fields (ten bands)     magic + version + 41 * u32 + checksum = 176 B

The checksum is FNV-1a 32 over every byte before the trailing checksum.

Examples:
    python cfweq_tool.py verify CFWEQ
    python cfweq_tool.py migrate CFWEQ CFWEQ.v4      # what the firmware does at boot
    python cfweq_tool.py defaults CFWEQ             # eight empty slots, default names
"""

import argparse
import struct
import sys

MAGIC = 0x51455743
CURRENT_VERSION = 6  # eight named slots; older files import into slot 1

TYPES = ("Off", "Peaking", "Low shelf", "High shelf")

V4_FREQUENCIES = (
    20, 25, 31, 40, 50, 60, 70, 80, 100, 125, 160, 200, 250, 315, 400, 500, 630, 800,
    1000, 1250, 1600, 2000, 2500, 3150, 4000, 5000, 5600, 6300, 7100, 8000, 9000,
    10000, 12500, 16000, 20000,
)
V4_Q = tuple(range(3, 61))
V4_GAIN = tuple(range(-120, 121))
V4_PRECUT = tuple(range(0, -101, -1))
V4_FIELDS = 1 + 4 * 10
V4_COUNTS = (len(V4_PRECUT),) + (4, len(V4_FREQUENCIES), len(V4_Q), len(V4_GAIN)) * 10
V5_SLOTS = 8  # tune slots the firmware file holds
V6_NAME_BYTES = 24  # bytes per slot name in a version 6 file

V3_FREQUENCIES = (
    20, 25, 31, 40, 50, 60, 70, 80, 100, 125, 160, 200, 250, 315, 400, 500, 630, 800,
    1000, 1250, 1600, 2000, 2500, 3150, 4000, 5000, 6300, 8000, 10000, 12500, 16000,
    20000,
)
V3_Q = tuple(range(3, 41))
V3_GAIN = tuple(range(-120, 121, 5))
V3_PRECUT = V4_PRECUT
V3_FIELDS = 21

def fnv1a(data):
    value = 2166136261
    for byte in data:
        value = ((value ^ byte) * 16777619) & 0xFFFFFFFF
    return value


def nearest(values, target):
    return min(range(len(values)), key=lambda index: abs(values[index] - target))


def pack(values, version):
    body = struct.pack("<II", MAGIC, version) + struct.pack(
        f"<{len(values)}I", *values
    )
    return body + struct.pack("<I", fnv1a(body))


def pack_v6(slots, active, names=None):
    """Build a version 6 file: header, eight names, eight slots, checksum."""
    padded = [list(slot) for slot in slots][:V5_SLOTS]
    while len(padded) < V5_SLOTS:
        padded.append(default_values())
    labels = list(names or [])[:V5_SLOTS]
    while len(labels) < V5_SLOTS:
        labels.append(f"Slot {len(labels) + 1}")
    body = struct.pack("<IIII", MAGIC, 6, V5_SLOTS, active)
    for name in labels:
        body += name.encode("utf-8")[: V6_NAME_BYTES - 1].ljust(V6_NAME_BYTES, b"\0")
    for slot in padded:
        body += struct.pack(f"<{V4_FIELDS}I", *slot)
    return body + struct.pack("<I", fnv1a(body))


def pack_single(values, name="Slot 1"):
    """A version 6 file whose tune and name sit in slot 1."""
    return pack_v6([values], 0, [name])


def slot_signature(values):
    """Short text signature of a tune: only the bands that do something."""
    parts = [f"pre {V4_PRECUT[values[0]] / 10:+.1f}"]
    for band in range(10):
        kind, hz, q, gain = values[1 + band * 4 : 5 + band * 4]
        if kind == 0 or gain == V4_GAIN.index(0):
            continue
        parts.append(
            f"{TYPES[kind][:2]}{V4_FREQUENCIES[hz]}:{V4_GAIN[gain] / 10:+.1f}"
        )
    return " ".join(parts)


def read_slots(path):
    """Return (version, slots, active, names, raw) for any CFWEQ version.

    Version 6 keeps eight named tunes; versions 5 and older hold fewer tunes and
    migrate into slot 1.
    """
    data = open(path, "rb").read()
    if len(data) < 64:
        raise ValueError(f"{path}: too short for any CFWEQ version ({len(data)} bytes)")
    magic, version = struct.unpack("<II", data[:8])
    if magic != MAGIC:
        raise ValueError(f"{path}: bad magic {magic:#x}")
    if version == 6:
        _, _, count, active = struct.unpack("<IIII", data[:16])
        if count != V5_SLOTS:
            raise ValueError(
                f"{path}: version 6 needs {V5_SLOTS} slots, header says {count}"
            )
        expected = 16 + V5_SLOTS * V6_NAME_BYTES + V5_SLOTS * V4_FIELDS * 4 + 4
        if len(data) != expected:
            raise ValueError(f"{path}: version 6 needs {expected} bytes, got {len(data)}")
        if struct.unpack("<I", data[-4:])[0] != fnv1a(data[:-4]):
            raise ValueError(f"{path}: checksum mismatch")
        if active >= count:
            raise ValueError(f"{path}: active slot {active} outside 0..{count - 1}")
        names = []
        for slot in range(V5_SLOTS):
            start = 16 + slot * V6_NAME_BYTES
            raw = data[start : start + V6_NAME_BYTES]
            end = raw.find(b"\0")
            if end < 0:
                raise ValueError(f"{path}: slot {slot + 1} name is not terminated")
            names.append(raw[:end].decode("utf-8", "replace"))
        base = 16 + V5_SLOTS * V6_NAME_BYTES
        slots = [
            list(struct.unpack_from(f"<{V4_FIELDS}I", data, base + slot * V4_FIELDS * 4))
            for slot in range(V5_SLOTS)
        ]
        for slot in slots:
            for index, value in enumerate(slot):
                if value >= V4_COUNTS[index]:
                    raise ValueError(f"{path}: slot value out of range at field {index}")
        return version, slots, active, names, data
    if version == 5:
        _, _, count, active = struct.unpack("<IIII", data[:16])
        if count != V5_SLOTS:
            raise ValueError(
                f"{path}: version 5 needs {V5_SLOTS} slots, header says {count}"
            )
        expected = 16 + V5_SLOTS * V4_FIELDS * 4 + 4
        if len(data) != expected:
            raise ValueError(f"{path}: version 5 needs {expected} bytes, got {len(data)}")
        if struct.unpack("<I", data[-4:])[0] != fnv1a(data[:-4]):
            raise ValueError(f"{path}: checksum mismatch")
        if active >= count:
            raise ValueError(f"{path}: active slot {active} outside 0..{count - 1}")
        slots = [
            list(struct.unpack_from(f"<{V4_FIELDS}I", data, 16 + slot * V4_FIELDS * 4))
            for slot in range(V5_SLOTS)
        ]
        for slot in slots:
            for index, value in enumerate(slot):
                if value >= V4_COUNTS[index]:
                    raise ValueError(f"{path}: slot value out of range at field {index}")
        return version, slots, active, [f"Slot {i + 1}" for i in range(V5_SLOTS)], data
    sizes = {1: 13, 2: 13, 3: 21, 4: 41}
    if version not in sizes:
        raise ValueError(f"{path}: unsupported version {version}")
    fields = sizes[version]
    expected = 8 + fields * 4 + 4
    if len(data) != expected:
        raise ValueError(f"{path}: version {version} needs {expected} bytes, got {len(data)}")
    stored = struct.unpack("<I", data[-4:])[0]
    if stored != fnv1a(data[:-4]):
        raise ValueError(f"{path}: checksum mismatch (stored {stored:#010x})")
    values = list(struct.unpack(f"<{fields}I", data[8:-4]))
    return version, [values], 0, data


def read(path):
    version, slots, active, _names, data = read_slots(path)
    return version, slots[active], data


def decode(version, values):
    """Turn stored indices into physical settings for display."""
    frequencies, q, gain = (
        (V3_FREQUENCIES, V3_Q, V3_GAIN)
        if version < 4
        else (V4_FREQUENCIES, V4_Q, V4_GAIN)
    )
    counts = (len(V4_PRECUT),) + (4, len(frequencies), len(q), len(gain)) * 10
    bands = 3 if version < 3 else (5 if version == 3 else 10)
    rows = []
    for index, value in enumerate(values):
        limit = counts[index] if index < len(counts) else 1
        if value >= limit:
            raise ValueError(f"field {index} index {value} outside 0..{limit - 1}")
    precut = V4_PRECUT[values[0]] if values[0] < len(V4_PRECUT) else 0
    for band in range(bands):
        kind, hz, qv, gv = values[1 + band * 4 : 5 + band * 4]
        rows.append((band + 1, TYPES[kind], frequencies[hz], q[qv] / 10, gain[gv] / 10))
    return precut / 10, rows


def convert(source_version, values):
    """Port of convert_bands() in payload/custom_eq.c."""
    if source_version >= 4:
        return values
    bands = 3 if source_version < 3 else 5
    frequencies, q, gain = (V3_FREQUENCIES, V3_Q, V3_GAIN)
    result = [0] * V4_FIELDS
    result[0] = values[0]
    for band in range(bands):
        kind, hz, qv, gv = values[1 + band * 4 : 5 + band * 4]
        target = 1 + band * 4
        result[target] = kind
        result[target + 1] = V4_FREQUENCIES.index(frequencies[hz])
        result[target + 2] = V4_Q.index(q[qv])
        result[target + 3] = V4_GAIN.index(gain[gv])
    # Bands the file does not carry keep the shipped defaults.
    defaults = default_values()
    for index in range(1 + bands * 4, V4_FIELDS):
        result[index] = defaults[index]
    return result


DEFAULT_BANDS = (
    (2, 60, 7, 0),
    (1, 1000, 7, 0),
    (3, 8000, 7, 0),
    (0, 250, 7, 0),
    (0, 4000, 7, 0),
    (0, 800, 7, 0),
    (0, 1600, 7, 0),
    (0, 3150, 7, 0),
    (0, 6300, 7, 0),
    (0, 12500, 7, 0),
)


def default_values():
    values = [0]  # precut 0.0 dB
    for kind, hz, qv, gv in DEFAULT_BANDS:
        values += [kind, V4_FREQUENCIES.index(hz), V4_Q.index(qv), gv + 120]
    return values


def report(title, precut, rows):
    print(f"{title}: precut {precut:+.1f} dB")
    print(f"  {'#':>2}  {'type':<10} {'frequency':>9} {'gain':>7} {'Q':>5}")
    for band, kind, hz, q, gain in rows:
        print(f"  {band:>2}  {kind:<10} {hz:>6} Hz {gain:>+6.1f} {q:>5.2f}")


def command_verify(args):
    version, slots, active, names, data = read_slots(args.path)
    where = f", slot {active + 1} of {len(slots)}" if version >= 5 else ""
    precut, rows = decode(version, slots[active])
    report(f"{args.path}: version {version}, {len(data)} bytes{where}", precut, rows)
    if args.slots and version >= 5:
        print("\nslots:")
        for index, values in enumerate(slots):
            marker = "*" if index == active else " "
            print(f" {marker} {index + 1}: {names[index]:<16} {slot_signature(values)}")
    if args.hex:
        for offset in range(0, min(len(data), 64), 16):
            chunk = data[offset : offset + 16]
            print(f"  {offset:04x}  " + " ".join(f"{byte:02x}" for byte in chunk))
    return 0


def command_rename(args):
    version, slots, active, names, _ = read_slots(args.path)
    if not 1 <= args.slot <= V5_SLOTS:
        raise ValueError(f"slot must be 1..{V5_SLOTS}")
    name = args.name
    if not name or len(name.encode("utf-8")) > V6_NAME_BYTES - 1:
        raise ValueError(f"name must be 1..{V6_NAME_BYTES - 1} UTF-8 bytes")
    if any(ord(character) < 0x20 or ord(character) == 0x7F for character in name):
        raise ValueError("name must not contain control characters")
    names[args.slot - 1] = name
    blob = pack_v6(slots, active, names)
    open(args.path, "wb").write(blob)
    print(f"wrote {args.path}: slot {args.slot} is now {name!r} ({len(blob)} bytes)")
    return 0


def command_migrate(args):
    version, slots, active, _names, _ = read_slots(args.source)
    if version >= 5:
        raise ValueError(
            f"{args.source}: already version {version} ({len(slots)} slots); "
            "pass it through unchanged instead of migrating"
        )
    converted = convert(version, slots[0])
    for index, value in enumerate(converted):
        if value >= V4_COUNTS[index]:
            raise ValueError(f"converted field {index} out of range: {value}")
    if args.out:
        blob = pack_single(converted, "Migrated")
        open(args.out, "wb").write(blob)
        print(f"wrote {args.out}: {len(blob)} bytes, version {CURRENT_VERSION}")
    precut, rows = decode(CURRENT_VERSION, converted)
    report(f"converted from version {version}", precut, rows)
    return 0


def command_defaults(args):
    """A version 6 file with eight empty slots and their default names."""
    values = [default_values() for _ in range(V5_SLOTS)]
    names = [f"Slot {index + 1}" for index in range(V5_SLOTS)]
    blob = pack_v6(values, 0, names)
    if args.out:
        open(args.out, "wb").write(blob)
        print(f"wrote {args.out}: {len(blob)} bytes, version {CURRENT_VERSION}")
    precut, rows = decode(CURRENT_VERSION, values[0])
    report("defaults, slot 1", precut, rows)
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)

    verify = sub.add_parser("verify", help="check magic, version and checksum")
    verify.add_argument("path")
    verify.add_argument("--hex", action="store_true", help="print the bytes")
    verify.add_argument("--slots", action="store_true", help="list every version 5 slot")
    verify.set_defaults(run=command_verify)

    migrate = sub.add_parser("migrate", help="convert an older file to the current version")
    migrate.add_argument("source")
    migrate.add_argument("out", nargs="?")
    migrate.set_defaults(run=command_migrate)

    defaults = sub.add_parser("defaults", help="write eight empty slots with default names")
    defaults.add_argument("out", nargs="?")
    defaults.set_defaults(run=command_defaults)

    rename = sub.add_parser("rename", help="set one slot's name in place")
    rename.add_argument("path")
    rename.add_argument("--slot", type=int, required=True)
    rename.add_argument("--name", required=True)
    rename.set_defaults(run=command_rename)

    args = parser.parse_args()
    try:
        return args.run(args)
    except (OSError, ValueError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
