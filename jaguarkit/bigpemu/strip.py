"""jaguarkit.bigpemu.strip - a tall strip of RAM as a picture.

findfb says where rows line up but not where the picture's top is: the score
hardly changes as the window slides a few rows.  This draws far more rows
than a picture has, from before a candidate base, so the top edge is
something to look at rather than something to infer.

    python -m jaguarkit.bigpemu.strip ram12.bin --base 0xADC00 --stride 1280 \\
        --back 80 --rows 400 --ppm strip.ppm

Needs numpy.
"""

import argparse
import sys

import numpy as np

from .findfb import rgb16_to_rgb24


def main(argv=None):
    ap = argparse.ArgumentParser(prog="python -m jaguarkit.bigpemu.strip")
    ap.add_argument("dump")
    ap.add_argument("--base", type=lambda s: int(s, 0), required=True)
    ap.add_argument("--width", type=int, default=320)
    ap.add_argument("--stride", type=int, help="default: width * 2")
    ap.add_argument("--back", type=int, default=80, help="rows before the base")
    ap.add_argument("--rows", type=int, default=400)
    ap.add_argument("--phase", type=int, default=0,
                    help="0 or 1: which of two phrase-interleaved buffers")
    ap.add_argument("--ppm", required=True)
    a = ap.parse_args(argv)

    stride = a.stride or a.width * 2
    ram = np.fromfile(a.dump, dtype=np.uint8)
    start = max(0, a.base - a.back * stride)
    n = min(a.rows, (ram.size - start) // stride)
    rows = ram[start:start + n * stride].reshape(n, stride)
    if stride != a.width * 2:
        rows = rows[:, ((np.arange(stride) // 8) % 2) == a.phase]
    rgb = rgb16_to_rgb24(rows.tobytes()).reshape(n, -1, 3)
    with open(a.ppm, "wb") as f:
        f.write(b"P6\n%d %d\n255\n" % (rgb.shape[1], rgb.shape[0]))
        f.write(rgb.tobytes())
    print("wrote %s: %dx%d from $%06X" % (a.ppm, rgb.shape[1], rgb.shape[0], start))
    return 0


if __name__ == "__main__":
    sys.exit(main())
