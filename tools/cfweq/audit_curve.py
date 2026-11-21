#!/usr/bin/env python3
"""Audit a CFWEQ preset for per-stage headroom, not just the summed response.

The firmware folds the precut into the first band's numerator, and Apple's stage
kernel may saturate to 16-bit between stages. A preset is therefore only safe when
every *prefix* of the cascade stays at or below 0 dBFS: a later cut cannot undo a
clip that already happened in an earlier stage.

    python audit_curve.py build/CFWEQ.ipod-tuned
    python audit_curve.py --rate 48000 build/CFWEQ.defaults
"""

import argparse
import sys

import cfweq_tool as tool
import eq_curve

POINTS = 3000


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("path", help="CFWEQ file")
    parser.add_argument("--rate", type=int, default=44100, choices=(44100, 48000))
    args = parser.parse_args()

    version, values, _ = tool.read(args.path)
    converted = tool.convert(version, values)
    bands = converted[1:]
    precut = -tool.V4_PRECUT[converted[0]]
    grid = [20 * 1000 ** (i / POINTS) for i in range(POINTS + 1)]

    print(
        f"{args.path}: version {version}, precut {-precut / 10:+.1f} dB, {args.rate} Hz"
    )

    cumulative = [0.0] * len(grid)
    worst_prefix = None
    worst_where = None
    for band in range(len(bands) // 4):
        kind, hz_index, q_index, gain_index = bands[band * 4 : band * 4 + 4]
        hz = tool.V4_FREQUENCIES[hz_index]
        q = tool.V4_Q[q_index]
        gain = tool.V4_GAIN[gain_index]
        coefficients = eq_curve.coefficients(
            kind, hz, q, gain, precut if band == 0 else 0, args.rate
        )
        stage = [eq_curve.response([coefficients], h, args.rate) for h in grid]
        cumulative = [a + b for a, b in zip(cumulative, stage)]
        peak = max(cumulative)
        where = grid[cumulative.index(peak)]
        flag = "" if peak <= 0.0 else "  <-- above 0 dBFS"
        print(
            f"  band {band + 1:>2} {tool.TYPES[kind]:<10} {hz:>6} Hz "
            f"{gain / 10:>+5.1f} dB Q {q / 10:>4.1f}   cumulative max "
            f"{peak:>+6.2f} dB at {where:>6.0f} Hz{flag}"
        )
        if worst_prefix is None or peak > worst_prefix:
            worst_prefix, worst_where = peak, where

    print(
        f"  worst cumulative peak {worst_prefix:+.2f} dB at {worst_where:.0f} Hz"
        f" ({'safe' if worst_prefix <= 0 else 'CLIPS'})"
    )
    return 0 if worst_prefix <= 0 else 1


if __name__ == "__main__":
    sys.exit(main())
