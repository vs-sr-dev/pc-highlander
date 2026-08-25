#!/usr/bin/env python3
"""filmls - list the Cinepak films on the FMV track.

The track is a plain concatenation of films, packed back to back and *not*
block aligned - the player seeks to a block and then scans forward for the
sync tag, which is why alignment does not matter.  Each film opens with the
header CINEPAK.INC describes:

    'FILM' size 0 0
    'FDSC' 20  'cvid' height width          - always 240 x 320 on this disc
    'CTAB' size  8 bytes, then entries of 16  - offset, size, timestamp, tag

Every CTAB entry points at one chunk.  A chunk opens with its own 64-byte sync
pad (the 4-byte tag repeated 16 times, the same convention the CD tracks use)
followed by 'STAB', a sample table of 16-byte entries - offset, size, timestamp,
type - which interleaves the audio with the video.  So the films carry their own
sound; there is no separate speech track on the retail disc.

Usage
    python tools/cinepak/filmls.py TRACK7 [--chunks N] [--tsv]
"""

import argparse
import struct
import sys

BLOCK = 2352


def films(d):
    """Every offset that really starts a film - 'FILM' also occurs in video."""
    out = []
    o = 0
    n = len(d)
    while True:
        o = d.find(b"FILM", o)
        if o < 0:
            break
        size = struct.unpack_from(">I", d, o + 4)[0]
        if 0 < size < 0x10000 and d[o + 16:o + 20] == b"FDSC":
            out.append(o)
            o += max(describe(d, o)["bytes"], 4)
        else:
            o += 4
    return out


def describe(d, o):
    size = struct.unpack_from(">I", d, o + 4)[0]
    _, codec, h, w = struct.unpack_from(">I4sII", d, o + 20)
    k = d.find(b"CTAB", o, o + size + 64)
    ctab = struct.unpack_from(">I", d, k + 4)[0]
    n = (ctab - 8) // 16          # 8 bytes of CTAB header, then 16 per chunk
    entries = [struct.unpack_from(">IIII", d, k + 16 + i * 16) for i in range(n)]
    last = entries[-1] if entries else (0, 0, 0, 0)
    total = last[0] + last[1]
    return dict(offset=o, block=o // BLOCK, header=size, codec=codec.decode(),
                width=w, height=h, chunks=n, bytes=total,
                # payload byte 0 sits 160 bytes into block 0 of the track
                seek=(o + 160) // BLOCK,
                ticks=last[2], entries=entries)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("track7")
    ap.add_argument("--chunks", type=int, metavar="N",
                    help="also print the chunk table of film N")
    ap.add_argument("--tsv", action="store_true")
    args = ap.parse_args()

    d = open(args.track7, "rb").read()
    offs = films(d)

    if args.tsv:
        print("film\tblock\toffset\tchunks\tbytes\tcodec\tw\th")
    for i, o in enumerate(offs):
        f = describe(d, o)
        if args.tsv:
            print("%d\t%d\t0x%x\t%d\t%d\t%s\t%d\t%d" %
                  (i, f["block"], f["offset"], f["chunks"], f["bytes"],
                   f["codec"], f["width"], f["height"]))
        else:
            print("film %2d  block %6d  offset 0x%09x  %3d chunks  %10d bytes"
                  "  %s %dx%d" % (i, f["block"], f["offset"], f["chunks"],
                                  f["bytes"], f["codec"], f["width"],
                                  f["height"]))

    if args.chunks is not None:
        f = describe(d, offs[args.chunks])
        print("\nchunk table of film %d:" % args.chunks)
        for j, (co, cs, ts, tag) in enumerate(f["entries"]):
            print("  %3d  offset 0x%08x  size %8d  t %6d  tag %r" %
                  (j, co, cs, ts, struct.pack(">I", tag)))

    print("\n%d films, %d blocks used of %d" %
          (len(offs), sum(describe(d, o)["bytes"] for o in offs) // BLOCK,
           len(d) // BLOCK), file=sys.stderr)


if __name__ == "__main__":
    main()
