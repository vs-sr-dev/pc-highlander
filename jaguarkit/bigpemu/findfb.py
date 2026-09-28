"""jaguarkit.bigpemu.findfb - find a framebuffer inside a dump of Jaguar RAM.

When a game's own addresses cannot be trusted (a source dump from before
release, or no source at all), the picture has to be found without being told
where it is.  The one thing a framebuffer has and nothing else in RAM does is
rows that line up at **exactly** its stride and not at a nearby one: a
photograph shifted eight pixels sideways stops matching, while a fill, a
gradient or empty memory does not care.  So the score of a window is

    mean |byte - byte one row below|  /  mean |byte - byte one row and
                                                8 pixels below|

near 1 for anything smooth or random, well under 1 only where a picture is
laid out at that stride.  Both halves are window sums over one cumulative sum,
so every candidate base is scored at once.

Two cheaper scores were tried first in Highlander's port and both fail
instructively: the row difference alone finds empty memory, and the row
difference over the window's spread finds the smoothest gradient allowed.

Two strides are tried: w*2 for a plain RGB16 buffer and w*4 for two buffers
interleaved a phrase (8 bytes) at a time, which is how Highlander's Cinepak
player double-buffers.

    python -m jaguarkit.bigpemu.findfb ram7.bin --ppm out.ppm
    python -m jaguarkit.bigpemu.findfb ram7.bin --width 320 --height 288

Needs numpy.  Checked with fakeram (a known frame planted in noise, both
layouts, found at the address it was planted) and on Highlander's dumps.
"""

import argparse
import sys

import numpy as np


def rgb16_to_rgb24(buf):
    """R5 B5 G6 words, big-endian, to RGB24 rows (see jaguarkit.pixels)."""
    px = np.frombuffer(buf, dtype=">u2").astype(np.uint32)
    r = (px >> 11) & 0x1F
    b = (px >> 6) & 0x1F
    g = px & 0x3F
    out = np.empty((px.size, 3), dtype=np.uint8)
    out[:, 0] = (r << 3) | (r >> 2)
    out[:, 1] = (g << 2) | (g >> 4)
    out[:, 2] = (b << 3) | (b >> 2)
    return out


def gather(ram, base, stride, row, h):
    """The rows of a picture at `base`, de-interleaved if stride is 2 rows."""
    if base + stride * h > ram.size:
        return None
    rows = ram[base:base + stride * h].reshape(h, stride)
    if stride == row:
        return rows
    keep = (np.arange(stride) // 8) % 2 == 0
    return rows[:, keep]


def find(ram, w, h, step=8, top=6, min_detail=6.0):
    """[(score, base, stride)], best first."""
    row = w * 2
    shift = 16                      # eight pixels, and a phrase multiple

    def window_mean(lag, span, starts):
        d = np.abs(ram[:-lag].astype(np.int16) -
                   ram[lag:].astype(np.int16)).astype(np.int64)
        c = np.concatenate(([0], np.cumsum(d)))
        return (c[starts + span] - c[starts]) / float(span)

    best = []
    for stride in (row, row * 2):
        span = stride * (h - 1)
        nb = (ram.size - span - stride - shift) // step
        if nb <= 0:
            continue
        starts = np.arange(nb) * step
        rows = window_mean(stride, span, starts)
        off = window_mean(stride + shift, span, starts)
        acc = np.where(off >= min_detail, rows / np.maximum(off, 1e-9), np.inf)
        for k in np.argsort(acc)[:top]:
            if np.isfinite(acc[k]):
                best.append((float(acc[k]), int(starts[k]), stride))
    best.sort()
    return best[:top]


def main(argv=None):
    ap = argparse.ArgumentParser(prog="python -m jaguarkit.bigpemu.findfb")
    ap.add_argument("dump")
    ap.add_argument("--width", type=int, default=320)
    ap.add_argument("--height", type=int, default=240)
    ap.add_argument("--ppm")
    ap.add_argument("--step", type=int, default=8,
                    help="candidate alignment; the OP wants phrases, so 8")
    ap.add_argument("--top", type=int, default=6)
    ap.add_argument("--min-detail", type=float, default=6.0)
    a = ap.parse_args(argv)

    ram = np.fromfile(a.dump, dtype=np.uint8)
    print("%s: %d bytes" % (a.dump, ram.size))
    best = find(ram, a.width, a.height, a.step, a.top, a.min_detail)
    if not best:
        sys.exit("no window looked like a picture")
    for s, base, stride in best:
        print("  score %6.3f  base $%06X  stride %d" % (s, base, stride))
    s, base, stride = best[0]
    print("winner: $%06X, stride %d" % (base, stride))
    if a.ppm:
        rows = gather(ram, base, stride, a.width * 2, a.height)
        rgb = rgb16_to_rgb24(rows.tobytes())
        with open(a.ppm, "wb") as f:
            f.write(b"P6\n%d %d\n255\n" % (a.width, a.height))
            f.write(rgb.tobytes())
        print("wrote", a.ppm)
    return 0


if __name__ == "__main__":
    sys.exit(main())
