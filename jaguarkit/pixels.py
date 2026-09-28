"""jaguarkit.pixels - the Jaguar's RGB16, and PPM files.

The Jaguar's 16-bit RGB pixel is **R5 B5 G6**, red in the top five bits, blue
in the next five and green in the low six - not the R5 G6 B5 of most other
machines.  Read with green in the middle, a blue picture comes out green,
which is the quickest way to tell the layout was got wrong.  (Highlander's
own source writes it down: "RBG = 5:5:6".)

Reducing 8-bit channels to it **rounds** rather than truncates.  A frame
taken out of the Jaguar's RAM under an emulator, against the same frame
decoded on a PC, puts truncation half a step low on every channel and
rounding at a mean error of zero (Highlander, docs/09-text-and-fmv.md 9.5).

Not here yet: CRY, the Jaguar's other 16-bit mode (a colour byte and an
intensity byte), which no game this kit has served has needed.
"""

import struct


def rgb16(r, g, b):
    """One pixel, 8-bit channels in, R5 B5 G6 out, rounded."""
    r5 = min((r + 4) >> 3, 31)
    b5 = min((b + 4) >> 3, 31)
    g6 = min((g + 2) >> 2, 63)
    return (r5 << 11) | (b5 << 6) | g6


def rgb24(p):
    """One R5 B5 G6 pixel out to 8-bit channels, the top bits replicated."""
    r = (p >> 11) & 0x1F
    b = (p >> 6) & 0x1F
    g = p & 0x3F
    return (r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)


def to_rgb16(rgb):
    """A packed RGB24 buffer to big-endian R5 B5 G6 words."""
    out = bytearray(len(rgb) // 3 * 2)
    for i in range(len(rgb) // 3):
        struct.pack_into(">H", out, i * 2,
                         rgb16(rgb[i * 3], rgb[i * 3 + 1], rgb[i * 3 + 2]))
    return bytes(out)


def from_rgb16(words):
    """Big-endian R5 B5 G6 words to a packed RGB24 buffer."""
    out = bytearray(len(words) // 2 * 3)
    for i in range(len(words) // 2):
        out[i * 3:i * 3 + 3] = bytes(rgb24(struct.unpack_from(">H", words, i * 2)[0]))
    return bytes(out)


def write_ppm(path, w, h, rgb):
    with open(path, "wb") as f:
        f.write(b"P6\n%d %d\n255\n" % (w, h))
        f.write(bytes(rgb))


def read_ppm(path):
    """(w, h, rgb bytes) from a binary PPM."""
    with open(path, "rb") as f:
        if f.readline().strip() != b"P6":
            raise ValueError("%s: not a binary PPM" % path)
        line = f.readline()
        while line.startswith(b"#"):
            line = f.readline()
        w, h = (int(v) for v in line.split())
        if f.readline().strip() != b"255":
            raise ValueError("%s: not 8 bits a channel" % path)
        return w, h, f.read(w * h * 3)
