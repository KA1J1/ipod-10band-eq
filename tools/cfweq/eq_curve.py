#!/usr/bin/env python3
"""Plot the response a RepriseOS 10-band CFWEQ file produces.

The coefficients are computed and quantized exactly like
`payload/eq_coefficients.c`, so the plotted curve is what the firmware's
Hold-mode canvas draws (the canvas samples the same function at 261 points).

    python eq_curve.py --file build/CFWEQ.defaults --out build/eq-curve.png
"""

import argparse
import math

import matplotlib

matplotlib.use("Agg")
import cfweq_tool as tool
import matplotlib.pyplot as plt

SCALE = 1 << 24


def coefficients(kind, hz, q_tenths, gain_tenths, precut_tenths, rate):
    """Port of cfw_eq_coefficients(); returns the quantized b0 b1 b2 a1 a2."""
    if (
        kind > 3
        or hz < 20
        or hz > 20000
        or q_tenths < 3
        or q_tenths > 60
        or not -120 <= gain_tenths <= 120
        or precut_tenths > 100
        or rate not in (44100, 48000)
    ):
        raise ValueError("filter outside the firmware limits")
    b0, b1, b2, a0, a1, a2 = 1.0, 0.0, 0.0, 1.0, 0.0, 0.0
    attenuation = 10 ** (-precut_tenths / 200)
    if kind != 0 and gain_tenths != 0:
        w = 2 * math.pi * hz / rate
        s, c = math.sin(w), math.cos(w)
        a = 10 ** (gain_tenths / 400)
        alpha = s * 5.0 / q_tenths
        beta = 2.0 * 10 ** (gain_tenths / 800) * alpha
        if kind == 1:
            b0, b1, b2 = 1 + alpha * a, -2 * c, 1 - alpha * a
            a0, a1, a2 = 1 + alpha / a, -2 * c, 1 - alpha / a
        elif kind == 2:
            b0 = a * ((a + 1) - (a - 1) * c + beta)
            b1 = 2 * a * ((a - 1) - (a + 1) * c)
            b2 = a * ((a + 1) - (a - 1) * c - beta)
            a0 = (a + 1) + (a - 1) * c + beta
            a1 = -2 * ((a - 1) + (a + 1) * c)
            a2 = (a + 1) + (a - 1) * c - beta
        else:
            b0 = a * ((a + 1) + (a - 1) * c + beta)
            b1 = -2 * a * ((a - 1) + (a + 1) * c)
            b2 = a * ((a + 1) + (a - 1) * c - beta)
            a0 = (a + 1) - (a - 1) * c + beta
            a1 = 2 * ((a - 1) - (a + 1) * c)
            a2 = (a + 1) - (a - 1) * c - beta
    values = [
        b0 * attenuation / a0,
        b1 * attenuation / a0,
        b2 * attenuation / a0,
        a1 / a0,
        a2 / a0,
    ]
    # The firmware rounds away from zero and then checks the fixed-point poles.
    out = [int(math.floor(v * SCALE + 0.5)) for v in values]
    if not (out[4] > -SCALE and out[4] < SCALE):
        raise ValueError("unstable fixed-point denominator")
    if SCALE + out[3] + out[4] <= 0 or SCALE - out[3] + out[4] <= 0:
        raise ValueError("unstable fixed-point poles")
    return out


def bank(version, values, rate):
    """Quantized coefficients for the stored settings, including precut."""
    frequencies, q, gain = (
        (tool.V3_FREQUENCIES, tool.V3_Q, tool.V3_GAIN)
        if version < 4
        else (tool.V4_FREQUENCIES, tool.V4_Q, tool.V4_GAIN)
    )
    bands = 3 if version < 3 else (5 if version == 3 else 10)
    precut = -tool.V4_PRECUT[values[0]]
    result = []
    for index in range(bands):
        kind, hz, qv, gv = values[1 + index * 4 : 5 + index * 4]
        result.append(
            coefficients(
                kind,
                frequencies[hz],
                q[qv],
                gain[gv],
                precut if index == 0 else 0,
                rate,
            )
        )
    return result


def response(coefficients_list, hz, rate):
    w = 2 * math.pi * hz / rate
    c, s = math.cos(w), math.sin(w)
    c2, s2 = c * c - s * s, 2 * s * c
    power = 1.0
    for b0, b1, b2, a1, a2 in coefficients_list:
        nr = b0 + b1 * c + b2 * c2
        ni = b1 * s + b2 * s2
        dr = SCALE + a1 * c + a2 * c2
        di = a1 * s + a2 * s2
        power *= (nr * nr + ni * ni) / (dr * dr + di * di)
    return 10 * math.log10(power)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--file", required=True, help="CFWEQ file to plot")
    parser.add_argument("--rate", type=int, default=44100, choices=(44100, 48000))
    parser.add_argument(
        "--normalize",
        type=float,
        metavar="HZ",
        help="plot the tonal balance relative to this frequency",
    )
    parser.add_argument("--out", default="build/eq-curve.png")
    args = parser.parse_args()

    version, values, _ = tool.read(args.file)
    converted = tool.convert(version, values)
    quantized = bank(4, converted, args.rate)
    points, curve = [], []
    steps = 400
    for index in range(steps + 1):
        hz = 20 * 1000 ** (index / steps)
        points.append(hz)
        curve.append(response(quantized, hz, args.rate))
    print(f"peak {max(curve):+.2f} dB, minimum {min(curve):+.2f} dB")

    reference = 0.0
    if args.normalize:
        reference = response(quantized, args.normalize, args.rate)
        curve = [value - reference for value in curve]
        print(f"reference {args.normalize:.0f} Hz at {reference:+.2f} dB set to 0 dB")
        print(f"balance: {curve[0]:+.1f} dB at 20 Hz, {max(curve):+.1f} dB peak")

    figure, axes = plt.subplots(figsize=(9, 4.5))
    label = f"RepriseOS 10-band ({args.rate} Hz)"
    if args.normalize:
        label += f", relative to {args.normalize:.0f} Hz"
    axes.semilogx(points, curve, label=label, linewidth=2)
    axes.set_xlim(20, 20000)
    low, high = min(curve), max(curve)
    margin = max(3.0, (high - low) * 0.15)
    axes.set_ylim(low - margin, high + margin)
    axes.set_xticks([20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000])
    axes.set_xticklabels(
        ["20", "50", "100", "200", "500", "1k", "2k", "5k", "10k", "20k"]
    )
    axes.grid(True, which="both", alpha=0.3)
    axes.set_xlabel("frequency (Hz)")
    axes.set_ylabel("gain (dB)")
    axes.set_title("Predicted Custom EQ response from quantized firmware coefficients")
    axes.axhline(0, color="black", linewidth=0.6)
    axes.legend(loc="lower left", fontsize=9)
    figure.tight_layout()
    figure.savefig(args.out, dpi=144)
    print(f"wrote {args.out}")


if __name__ == "__main__":
    main()
