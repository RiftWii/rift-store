# SPDX-FileCopyrightText: 2026 RiftWii contributors
# SPDX-License-Identifier: GPL-3.0-or-later

"""Synthesize short, original storefront UI cues as aligned mono PCM."""

from itertools import batched
from math import exp, sin, tau
from pathlib import Path
import sys


RATE = 24000

# Start time, oscillator frequency, note length and peak amplitude.
CUES = {
    "move": ((0, 920, .050, .23),),
    "open": ((0, 554.37, .105, .30), (.043, 830.61, .140, .23)),
    "back": ((0, 659.25, .078, .25), (.036, 440, .100, .20)),
    "page": ((0, 440, .066, .21), (.027, 698.46, .120, .28)),
    "complete": ((0, 523.25, .160, .26), (.058, 659.25, .160, .24), (.120, 880, .233, .23)),
    "remove_prompt": ((0, 370, .055, .24), (.041, 554.37, .084, .18)),
    "remove_done": ((0, 622.25, .083, .27), (.062, 415.30, .094, .24), (.132, 277.18, .150, .23)),
}


def synthesize(notes):
    frames = round(RATE * max(start + length for start, _, length, _ in notes))
    result = []
    for frame in range(frames):
        value = 0.0
        for start, frequency, length, peak in notes:
            elapsed = frame / RATE - start
            if not 0 <= elapsed < length:
                continue
            envelope = min(1, elapsed / .005, (length - elapsed) / .018) * exp(-3.2 * elapsed / length)
            phase = tau * frequency * elapsed
            tone = .83 * sin(phase) + .13 * sin(phase * 2) + .04 * sin(phase * 3.01)
            value += peak * envelope * tone
        result.append(round(value * 32767))
    # ASND reads complete 32-byte blocks. PCM is stored in 16-bit samples.
    result.extend([0] * (-len(result) % 16))
    return result


with Path(sys.argv[1]).open("w", encoding="utf-8", newline="\n") as output:
    output.write(f"#define CUE_SAMPLE_RATE {RATE}\n")
    for name, notes in CUES.items():
        output.write(f"static const s16 pcm_{name}[] ATTRIBUTE_ALIGN(32) = {{\n")
        for line in batched(synthesize(notes), 16):
            output.write("    " + ", ".join(map(str, line)) + ",\n")
        output.write("};\n")
