#!/usr/bin/env python3
"""Dump a tall strip of memory as an image, to see where a picture starts.

`findfb.py` says where the rows line up but not where the top of the picture
is: the score barely changes if the window slides a few rows up or down, so it
settles wherever it likes.  This draws far more rows than a picture has, from
before the candidate base, and the top edge is then something to look at rather
than something to infer.

  python tools/emu/strip.py ram12.bin --base 0xADC00 --back 80 --rows 400 \\
      --ppm strip.ppm
"""

import argparse

import numpy as np

W = 320


def rgb16_to_rgb24(buf):
    # R5 B5 G6 - COLLECT.GAS's own "RBG = 5:5:6", not R5 G6 B5.
    px = np.frombuffer(buf, dtype=">u2").astype(np.uint32)
    r = (px >> 11) & 0x1F
    b = (px >> 6) & 0x1F
    g = px & 0x3F
    out = np.empty((px.size, 3), dtype=np.uint8)
    out[:, 0] = (r << 3) | (r >> 2)
    out[:, 1] = (g << 2) | (g >> 4)
    out[:, 2] = (b << 3) | (b >> 5)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dump")
    ap.add_argument("--base", type=lambda s: int(s, 0), required=True)
    ap.add_argument("--stride", type=int, default=1280)
    ap.add_argument("--back", type=int, default=80, help="rows before the base")
    ap.add_argument("--rows", type=int, default=400)
    ap.add_argument("--phase", type=int, default=0,
                    help="0 or 1: which of the two interleaved buffers")
    ap.add_argument("--ppm", required=True)
    args = ap.parse_args()

    ram = np.fromfile(args.dump, dtype=np.uint8)
    start = args.base - args.back * args.stride
    start = max(0, start)
    n = min(args.rows, (ram.size - start) // args.stride)
    rows = ram[start:start + n * args.stride].reshape(n, args.stride)
    if args.stride != W * 2:
        keep = ((np.arange(args.stride) // 8) % 2) == args.phase
        rows = rows[:, keep]
    rgb = rgb16_to_rgb24(rows.tobytes()).reshape(n, -1, 3)
    with open(args.ppm, "wb") as f:
        f.write(b"P6\n%d %d\n255\n" % (rgb.shape[1], rgb.shape[0]))
        f.write(rgb.tobytes())
    print("wrote %s: %dx%d from $%06X" % (args.ppm, rgb.shape[1], rgb.shape[0],
                                          start))


if __name__ == "__main__":
    main()
