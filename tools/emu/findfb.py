#!/usr/bin/env python3
"""Find the Cinepak player's framebuffer inside a dump of Jaguar RAM.

`tools/emu/hl_cine.c` takes the whole 2 MB while a film is on screen, because
the addresses CINEPAK.INC fixes turned out not to hold on the retail disc: with
the boot film visibly playing, $0010E000 holds 12441245, which is two RGB16
pixels rather than the 'FILM' tag FindFilm looks for.  So the picture is found
here instead, and found without being told where it is.

The test is the one thing a framebuffer has and nothing else in RAM does: its
rows repeat.  A 320x240 16-bit picture is 640 bytes a row, and consecutive rows
of a photograph agree far more often than two arbitrary 640-byte runs of code
or of compressed data.

But "rows agree" on its own is not enough, and two versions of this said so.
Scored on the row difference alone it found a black hole: empty memory agrees
with itself perfectly, and 2 MB of Jaguar RAM has a great deal of empty in it.
Scored on the row difference over the window's standard deviation it found the
flattest window the spread floor would let it have, wherever that floor was
put - the optimum was always "as smooth as allowed", which is a gradient, not
a picture.

What actually distinguishes a framebuffer is that its rows line up at
**exactly** one stride and not at a nearby one.  A photograph shifted eight
pixels sideways stops matching; a gradient, a fill or a smooth ramp does not
care.  So the score is

    mean |byte - byte one row below|  /  mean |byte - byte one row and
                                                8 pixels below|

which is near 1 for anything smooth or anything random, and well under 1 only
where a real picture is laid out at that exact stride.  Both halves are window
sums, so both are one cumulative sum over the whole dump and every candidate
base is scored at once.  Two strides are tried, because
CINEPAK.S's double buffer interleaves the two pictures a phrase at a time
(screen_gap 2), which doubles the byte stride without changing the width:

    640    a plain 320x240x2 buffer
    1280   two of them interleaved, 8 bytes of one then 8 bytes of the other

  python tools/emu/findfb.py BigPEmuDEV/Scripts/hlcine/ram7.bin --ppm out.ppm

prints the best few candidates and writes the winner as a PPM.
"""

import argparse
import sys

import numpy as np

W, H, BPP = 320, 240, 2
ROW = W * BPP                       # 640 bytes of picture in a row


def rgb16_to_rgb24(buf):
    """The Jaguar's RGB16, which is **R5 B5 G6** and not R5 G6 B5.

    `COLLECT.GAS`'s `darken_screen` writes the layout down in its own comment -
    "RBG = 5:5:6" - and `src/media/cinepak.c` has always packed it that way.
    This file did not, for one evening, and the picture came out green where
    the film is blue: read with green in the middle, a blue vortex becomes a
    green one.  It is a good reminder that a comparator is a piece of code too.
    """
    px = np.frombuffer(buf, dtype=">u2").astype(np.uint32)
    r = (px >> 11) & 0x1F
    b = (px >> 6) & 0x1F
    g = px & 0x3F
    out = np.empty((px.size, 3), dtype=np.uint8)
    out[:, 0] = (r << 3) | (r >> 2)
    out[:, 1] = (g << 2) | (g >> 4)
    out[:, 2] = (b << 3) | (b >> 5)
    return out


def gather(ram, base, stride):
    """The H rows of a picture at `base`, de-interleaved if stride is 1280.

    A stride of 1280 means the phrases alternate between the two buffers, so
    the picture's own bytes are the phrases at even multiples of 8 within each
    1280-byte row: 8 bytes taken, 8 bytes skipped, forty times over."""
    if base + stride * H > ram.size:
        return None
    rows = ram[base:base + stride * H].reshape(H, stride)
    if stride == ROW:
        return rows
    keep = (np.arange(stride) // 8) % 2 == 0
    return rows[:, keep]


def spread(ram, base, stride):
    rows = gather(ram, base, stride)
    return 0.0 if rows is None else float(rows.std())


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dump")
    ap.add_argument("--ppm")
    ap.add_argument("--step", type=int, default=8,
                    help="candidate alignment (the OP wants phrases, so 8)")
    ap.add_argument("--top", type=int, default=6)
    ap.add_argument("--min-detail", type=float, default=6.0,
                    help="reject windows with too little going on to be a picture")
    args = ap.parse_args()

    ram = np.fromfile(args.dump, dtype=np.uint8)
    print("%s: %d bytes" % (args.dump, ram.size))

    def window_mean(lag, span, starts):
        """Mean |x[i] - x[i+lag]| over each window, for every start at once."""
        d = np.abs(ram[:-lag].astype(np.int16) -
                   ram[lag:].astype(np.int16)).astype(np.int64)
        c = np.concatenate(([0], np.cumsum(d)))
        return (c[starts + span] - c[starts]) / float(span)

    # Eight pixels sideways.  Any multiple of 8 keeps the phrase parity of the
    # interleaved layout, so the comparison stays inside the same buffer and
    # measures the picture rather than the interleave.
    SHIFT = 16

    best = []
    for stride in (ROW, ROW * 2):
        span = stride * (H - 1)
        if ram.size <= span + stride + SHIFT:
            continue
        nb = (ram.size - span - stride - SHIFT) // args.step
        if nb <= 0:
            continue
        starts = np.arange(nb) * args.step
        rows = window_mean(stride, span, starts)
        off = window_mean(stride + SHIFT, span, starts)
        # `off` also says how much is going on at all: a window of zeroes has
        # nothing to tell apart and is not a picture.
        acc = np.where(off >= args.min_detail, rows / np.maximum(off, 1e-9),
                       np.inf)
        order = np.argsort(acc)[:args.top]
        for k in order:
            if not np.isfinite(acc[k]):
                continue
            best.append((float(acc[k]), int(starts[k]), stride, float(rows[k]),
                         float(off[k])))

    best.sort()
    if not best:
        sys.exit("no candidate window looked like a picture")
    for s, base, stride, rw, of in best[:args.top]:
        print("  score %6.3f  base $%06X  stride %d  rows %6.2f  shifted %6.2f"
              % (s, base, stride, rw, of))
    best = [(s, b, st) for s, b, st, _, _ in best]

    s, base, stride = best[0]
    print("winner: $%06X, stride %d" % (base, stride))
    if args.ppm:
        rows = gather(ram, base, stride)
        rgb = rgb16_to_rgb24(rows.tobytes()).reshape(H, W, 3)
        with open(args.ppm, "wb") as f:
            f.write(b"P6\n%d %d\n255\n" % (W, H))
            f.write(rgb.tobytes())
        print("wrote", args.ppm)


if __name__ == "__main__":
    main()
