#!/usr/bin/env python3
"""animx - extract the character animations.

Track 8 is animations end to end; slots 2, 4 and 6 of track 5 are as well.  Each
record is preceded by a long holding its length and then follows the `ANIM`
header of `STRUCDEF.INC` unchanged:

    +0   .w  ANIMSIZE      total, header included
    +2   .w  OFFSETLOW     offset to the frame data, always 14
    +4   .w  FRAMESIZE     bytes per frame
    +6   .b  ANIMMODELS    animated pieces, 15 for every character on the disc
    +7   .b  NUMFRAMES
    +8   .b  ANIMFPS       20 throughout
    +9   .b  SOUNDSHEET
    +10  .w  SOUNDENTRY
    +12  .w  HEIGHTSTART

A frame is 20 bytes of root motion and combat data, then three angle bytes per
animated piece, rounded up to an even length:

    +0   .w  x move            root motion
    +2   .w  y move
    +4   .w  z move
    +6   .b  y turn
    +7   .b  flags
    +8   .b  spin
    +9   .b  hit               positive attack, negative defence
    +10  .w  range             attack reach
    +12  .b  direction az
    +13  .b  direction el
    +14  .b  spread az
    +15  .b  spread el
    +16  .w  high              highest point of the character
    +18  .w  low               lowest point
    +20  ..  3 bytes per piece: rotations

So `FRAMESIZE == 20 + 3 * ANIMMODELS` rounded up to even - 66 for 15 pieces -
and `ANIMSIZE == 14 + FRAMESIZE * NUMFRAMES`, also rounded.  Both hold for every
record on the disc, which is what identifies an animation record in the first
place.

The animation model is hierarchical rotations: a frame stores only three angles
per piece plus the root motion, and the in-between frames are computed at
runtime.

Usage
    python tools/anim/animx.py FILE [--list] [--json FILE] [--index N]
"""

import argparse
import json
import struct
import sys

HEADER = 14


def parse(d, o):
    """Parse the animation whose length prefix is at o, or return None."""
    if o + 4 + HEADER > len(d):
        return None
    total = struct.unpack_from(">I", d, o)[0]
    if not HEADER < total <= 0x20000 or o + 4 + total > len(d):
        return None
    (size, hdr, framesize, models, frames, fps, sheet, entry,
     hstart) = struct.unpack_from(">HHHBBBBHh", d, o + 4)
    if hdr != HEADER or size != total or not (0 < frames and 0 < models <= 64):
        return None
    if framesize != (20 + 3 * models + 1) & ~1:
        return None
    if not HEADER + framesize * frames <= size <= HEADER + framesize * frames + 8:
        return None

    fo = o + 4 + HEADER
    out = []
    for i in range(frames):
        b = fo + i * framesize
        xm, ym, zm = struct.unpack_from(">3h", d, b)
        turn, flags, spin, hit = struct.unpack_from(">4b", d, b + 6)
        rng = struct.unpack_from(">h", d, b + 10)[0]
        daz, dele, saz, sel = struct.unpack_from(">4b", d, b + 12)
        high, low = struct.unpack_from(">2h", d, b + 16)
        angles = [list(struct.unpack_from(">3b", d, b + 20 + p * 3))
                  for p in range(models)]
        out.append(dict(move=[xm, ym, zm], turn=turn, flags=flags, spin=spin,
                        hit=hit, range=rng, dir=[daz, dele], spread=[saz, sel],
                        high=high, low=low, angles=angles))
    return dict(offset=o, size=size, framesize=framesize, models=models,
                frames=frames, fps=fps, sound_sheet=sheet, sound_entry=entry,
                height_start=hstart, frame_data=out)


def scan(d):
    out = []
    o = 0
    while o + 4 + HEADER < len(d):
        a = parse(d, o)
        if a:
            out.append(a)
            o += 4 + a["size"]
        else:
            o += 2
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("file")
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--json", metavar="FILE")
    ap.add_argument("--index", type=int)
    ap.add_argument("--frames", action="store_true",
                    help="print the root motion of each frame")
    args = ap.parse_args()

    d = open(args.file, "rb").read()
    anims = scan(d)
    chosen = [anims[args.index]] if args.index is not None else anims

    if args.list or not args.json:
        print("%-4s %-10s %6s %6s %5s %4s %6s %s" %
              ("#", "offset", "frames", "pieces", "fps", "size", "sheet",
               "seconds"))
        for i, a in enumerate(chosen):
            print("%-4d 0x%08x %6d %6d %5d %4d %6d %7.2f" %
                  (i, a["offset"], a["frames"], a["models"], a["fps"],
                   a["size"], a["sound_sheet"], a["frames"] / a["fps"]))
            if args.frames:
                for j, f in enumerate(a["frame_data"]):
                    print("    %3d  move %5d %5d %5d  turn %4d  hit %4d "
                          "range %5d" % (j, f["move"][0], f["move"][1],
                                         f["move"][2], f["turn"], f["hit"],
                                         f["range"]))

    if args.json:
        with open(args.json, "w") as f:
            json.dump(chosen, f, indent=1)
        print("wrote %s" % args.json)

    total = sum(a["frames"] / a["fps"] for a in anims)
    print("%d animations, %.1f seconds of animation" % (len(anims), total),
          file=sys.stderr)


if __name__ == "__main__":
    main()
