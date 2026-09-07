#!/usr/bin/env python3
"""Compare the Jaguar's own decoded film frame against ours.

This is the frame comparator's last mile and the answer to
docs/09-text-and-fmv.md 9.5: our decoder goes cvid -> YUV -> 24-bit -> RGB16,
and the Jaguar's went straight to RGB16, so the two may round differently.
Nobody could say until there was a picture from each of the same frame.

It runs in three steps, and each is a search that cannot be skipped because
nothing in the dump says which frame it is looking at:

  1. take the picture out of the dump - `findfb.py` gives the row stride, and
     the horizontal roll is the column where the wrap seam is;
  2. find *which* frame it is, by colour histogram, which does not care about
     the vertical offset that is still unknown;
  3. find that offset by trying every row, and only then compare pixel for
     pixel.

  python tools/emu/cmpfb.py BigPEmuDEV/Scripts/hlcine/ram12.bin \\
      --track assets/tracks/track07_1111.bin --film 34
"""

import argparse
import os
import sys

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "cinepak"))
import filmdec                                          # noqa: E402

W, H = 320, 240
STRIDE = 1280


def jag_picture(path, base, rows, roll):
    """The Jaguar's framebuffer, as `rows` rows of 320 RGB16 words.

    The buffer is phrase-interleaved - CINEPAK.S's `screen_gap 2`, two buffers
    eight bytes at a time - so half the bytes belong to the other one.  Both
    normally hold the same picture, which is itself worth knowing: it is what
    says the layout has been read right."""
    ram = np.fromfile(path, dtype=np.uint8)
    n = min(rows, (ram.size - base) // STRIDE)
    keep = ((np.arange(STRIDE) // 8) % 2) == 0
    r = np.ascontiguousarray(ram[base:base + n * STRIDE].reshape(n, STRIDE)[:, keep])
    px = np.frombuffer(r.tobytes(), dtype=">u2").reshape(n, W)
    return np.roll(px, -roll, axis=1)


def seam(px):
    """Which column the row boundary really is, from the wrap discontinuity."""
    lum = ((px >> 11) & 31) * 8 + (px & 63) * 4 + ((px >> 6) & 31) * 8
    band = lum.astype(np.int32)
    h = np.abs(np.diff(band, axis=1)).mean(axis=0)
    wrap = np.abs(band[:, 0] - band[:, -1]).mean()
    allb = np.concatenate((h, [wrap]))
    return int(allb.argmax()) + 1, float(allb.max()), float(np.median(allb))


def to565(rgb24, rounded=True):
    """Our 24-bit RGB reduced to the Jaguar's RGB16.

    The layout is R5 B5 G6 - COLLECT.GAS's "RBG = 5:5:6" - and the reduction
    is the thing on trial: `rounded` False is truncation, which is what
    `cinepak_rgb16` used to do."""
    r = rgb24[:, :, 0].astype(np.int32)
    g = rgb24[:, :, 1].astype(np.int32)
    b = rgb24[:, :, 2].astype(np.int32)
    if rounded:
        r, g, b = (r + 4) >> 3, (g + 2) >> 2, (b + 4) >> 3
        r = np.minimum(r, 31); g = np.minimum(g, 63); b = np.minimum(b, 31)
    else:
        r, g, b = r >> 3, g >> 2, b >> 3
    return ((r << 11) | (b << 6) | g).astype(np.uint16)


def hist(px):
    """A signature that does not care where the picture starts."""
    r = ((px >> 11) & 31).ravel()
    b = ((px >> 6) & 31).ravel()
    g = (px & 63).ravel()
    h = np.concatenate((np.bincount(r, minlength=32),
                        np.bincount(g, minlength=64),
                        np.bincount(b, minlength=32))).astype(np.float64)
    return h / max(h.sum(), 1)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dump")
    ap.add_argument("--track", default="assets/tracks/track07_1111.bin")
    ap.add_argument("--film", type=int, default=34)
    ap.add_argument("--base", type=lambda s: int(s, 0), default=0x0C0000)
    ap.add_argument("--roll", type=int, default=None,
                    help="default: found from the wrap seam")
    ap.add_argument("--rows", type=int, default=300)
    ap.add_argument("--max-frames", type=int, default=0)
    ap.add_argument("--refine", type=int, default=10,
                    help="frames either side of the histogram pick to try")
    ap.add_argument("--ppm-jag")
    ap.add_argument("--ppm-ours")
    args = ap.parse_args()

    raw = jag_picture(args.dump, args.base, args.rows, 0)
    roll = args.roll
    if roll is None:
        roll, hi, med = seam(raw[40:220])
        print("seam at column %d (step %.1f against a median of %.1f)"
              % (roll - 1, hi, med))
    jag = np.roll(raw, -roll, axis=1)
    print("picture taken from $%06X, stride %d, rolled %d"
          % (args.base, STRIDE, roll))

    d = open(args.track, "rb").read()
    offs = filmdec.films(d)
    info = filmdec.describe(d, offs[args.film])
    vid = filmdec.video(d, offs[args.film])
    print("film %d: %dx%d, %d frames" % (args.film, info["width"],
                                         info["height"], len(vid)))

    jh = hist(jag[40:220])
    dec = filmdec.Decoder(info["width"], info["height"])
    n = len(vid) if not args.max_frames else min(args.max_frames, len(vid))
    best = (1e9, -1)
    for i in range(n):
        p, sz, ts, dur = vid[i]
        dec.frame(d, p, sz)
        ours = np.frombuffer(bytes(dec.rgb), dtype=np.uint8).reshape(H, W, 3)
        # The same band of rows, wherever the picture starts vertically: a
        # histogram over most of the picture is stable against a shift of a
        # few dozen rows, which is all the offset can be.
        oh = hist(to565(ours)[40:220])
        d2 = float(np.abs(jh - oh).sum())
        if d2 < best[0]:
            best = (d2, i)
        if (i % 100) == 0:
            print("  ... frame %d, best so far %d at %.4f" % (i, best[1], best[0]))
    print("closest frame: %d, histogram distance %.4f" % (best[1], best[0]))

    # A histogram cannot tell one frame from the one next to it, and on a slow
    # pan two neighbours have nearly the same colours.  So decode a window
    # around the pick and choose on the pixels themselves, over every vertical
    # offset - the frame and the offset are found together because neither
    # means anything without the other.
    lo = max(0, best[1] - args.refine)
    hi = min(len(vid) - 1, best[1] + args.refine)
    dec = filmdec.Decoder(info["width"], info["height"])
    fine = (1e18, -1, -1)
    for i in range(hi + 1):
        p, sz, ts, dur = vid[i]
        dec.frame(d, p, sz)
        if i < lo:
            continue
        cand = to565(np.frombuffer(bytes(dec.rgb),
                                   dtype=np.uint8).reshape(H, W, 3))
        for r in range(0, jag.shape[0] - H + 1):
            e = float(np.abs(jag[r:r + H].astype(np.int32) -
                             cand.astype(np.int32)).mean())
            if e < fine[0]:
                fine = (e, i, r)
    err, frame, row = fine
    print("on the pixels: frame %d at row %d ($%06X), mean |RGB16 word| %.1f"
          % (frame, row, args.base + row * STRIDE, err))

    dec = filmdec.Decoder(info["width"], info["height"])
    for i in range(frame + 1):
        p, sz, ts, dur = vid[i]
        dec.frame(d, p, sz)
    ours24 = np.frombuffer(bytes(dec.rgb), dtype=np.uint8).reshape(H, W, 3)
    ours = to565(ours24)

    cut = jag[row:row + H]
    jr = ((cut >> 11) & 31).astype(np.int32)
    jb = ((cut >> 6) & 31).astype(np.int32)
    jg = (cut & 63).astype(np.int32)

    # The question 9.5 left open, put both ways round.  Truncation is what this
    # port did; rounding to nearest is what the hardware turns out to do.
    for label, rounded in (("truncated", False), ("rounded", True)):
        px = to565(ours24, rounded)
        orr = ((px >> 11) & 31).astype(np.int32)
        ob = ((px >> 6) & 31).astype(np.int32)
        og = (px & 63).astype(np.int32)
        print()
        print("ours %s: %.3f%% of pixels identical to the Jaguar's"
              % (label, 100.0 * (px == cut).mean()))
        print("  channel  identical   ours low   ours high   mean error  "
              "within one step")
        for name, a, b in (("R", orr, jr), ("G", og, jg), ("B", ob, jb)):
            d_ = a - b
            print("    %s      %7.3f%%   %7.3f%%   %7.3f%%      %+6.3f      "
                  "%7.3f%%"
                  % (name, 100.0 * (d_ == 0).mean(), 100.0 * (d_ < 0).mean(),
                     100.0 * (d_ > 0).mean(), d_.mean(),
                     100.0 * (np.abs(d_) <= 1).mean()))

    def write(path, px):
        r_ = ((px >> 11) & 31).astype(np.uint8)
        b_ = ((px >> 6) & 31).astype(np.uint8)
        g_ = (px & 63).astype(np.uint8)
        rgb = np.dstack(((r_ << 3) | (r_ >> 2), (g_ << 2) | (g_ >> 4),
                         (b_ << 3) | (b_ >> 5)))
        with open(path, "wb") as f:
            f.write(b"P6\n%d %d\n255\n" % (px.shape[1], px.shape[0]))
            f.write(rgb.tobytes())
        print("wrote", path)

    if args.ppm_jag:
        write(args.ppm_jag, cut)
    if args.ppm_ours:
        write(args.ppm_ours, ours)


if __name__ == "__main__":
    main()
