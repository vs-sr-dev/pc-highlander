#!/usr/bin/env python3
"""scenex - extract the pre-rendered backdrops and Z-buffers from the PICT track.

Each scene slot is 258,720 bytes (110 blocks):

    +0        256,000  payload: 64,000 colour pixels then 64,000 depth values,
                       both 16 bit, 320x200, XOR-obfuscated (see below)
    +256,000       48  camera footer, view matrix in s1.14
    +256,048    2,608  zero fill
    +258,656       64  the tag "PICT" repeated 16 times, introducing the next slot

The payload is not compressed - it is XORed with an 8,192-byte key that lives in
the resident binary at address $30610.  The game does this on the GPU: the scene
module blits the key over the payload with B_CMD = $C00009, that is SRCEN|DSTEN
with the logic function (~A&B)|(A&~B) = XOR, A2_BASE = $30610 and A2_MASK = $7FF.
The mask applies to the pixel index and the blit runs at 32 bits per pixel, so
the key repeats every 2,048 pixels = 8,192 bytes.  It is applied twice, once per
128,000-byte half, with A2_PIXEL reset in between - so the key index restarts at
the halfway point rather than running straight through.

Usage
    python tools/scene/scenex.py TRACK4 --boot TRACK2 --out DIR
    python tools/scene/scenex.py TRACK4 --boot TRACK2 --out DIR --range 0 20
    python tools/scene/scenex.py TRACK4 --boot TRACK2 --out DIR --scene 300 --raw
"""

import argparse
import os
import struct
import sys

import numpy as np

SLOT = 258720
PAYLOAD = 256000
HALF = 128000
FOOTER = 256000
WIDTH, HEIGHT = 320, 200
KEY_ADDR = 0x30610
KEY_LEN = 8192
CODE_BASE = 0x5000


def find_key(boot):
    """The XOR key, read out of the resident binary in the boot track."""
    tag = b"CODE" * 16
    i = boot.find(tag)
    if i < 0:
        raise SystemExit("no CODE section in the boot track - wrong file?")
    code_off = i + len(tag)                 # this byte is address $5000
    off = code_off + (KEY_ADDR - CODE_BASE)
    return boot[off:off + KEY_LEN]


def decode(payload, key):
    """Undo the XOR.  The key restarts at the halfway point."""
    k = np.frombuffer(key, dtype=np.uint8)
    pad = np.resize(k, HALF)                # 128,000 is not a multiple of 8,192
    out = np.frombuffer(payload, dtype=np.uint8).copy()
    out[:HALF] ^= pad
    out[HALF:] ^= pad
    return out


def to_rgb(words):
    """Jaguar RGB16 is R5 B5 G6, not RGB565."""
    r = ((words >> 11) & 0x1F).astype(np.uint16) * 255 // 31
    b = ((words >> 6) & 0x1F).astype(np.uint16) * 255 // 31
    g = (words & 0x3F).astype(np.uint16) * 255 // 63
    return np.dstack([r, g, b]).astype(np.uint8).reshape(HEIGHT, WIDTH, 3)


def camera(footer):
    """The 48-byte footer as 24 s1.14 words and 12 longs, for inspection."""
    words = struct.unpack(">24h", footer)
    longs = struct.unpack(">12i", footer)
    return words, longs


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("track4")
    ap.add_argument("--boot", required=True, help="extracted track 2")
    ap.add_argument("--out", required=True)
    ap.add_argument("--scene", type=int)
    ap.add_argument("--range", nargs=2, type=int, metavar=("FIRST", "LAST"))
    ap.add_argument("--depth", action="store_true",
                    help="also write the Z-buffer as a lossless 16-bit PNG")
    ap.add_argument("--depth-view", action="store_true",
                    help="also write a contrast-stretched 8-bit view of the Z")
    ap.add_argument("--raw", action="store_true",
                    help="also write the decoded 256,000-byte payload")
    ap.add_argument("--camera", action="store_true",
                    help="print the camera footer of each scene")
    args = ap.parse_args()

    from PIL import Image

    key = find_key(open(args.boot, "rb").read())
    size = os.path.getsize(args.track4)
    count = size // SLOT
    os.makedirs(args.out, exist_ok=True)

    if args.scene is not None:
        first, last = args.scene, args.scene
    elif args.range:
        first, last = args.range
    else:
        first, last = 0, count - 1
    last = min(last, count - 1)

    fh = open(args.track4, "rb")
    for n in range(first, last + 1):
        fh.seek(n * SLOT)
        slot = fh.read(SLOT)
        data = decode(slot[:PAYLOAD], key)
        words = data.view(">u2") if data.dtype == np.uint8 else data
        words = np.frombuffer(data.tobytes(), dtype=">u2")
        colour, depth = words[:64000], words[64000:]

        Image.fromarray(to_rgb(colour)).save(
            os.path.join(args.out, "scene%03d.png" % n))
        if args.depth:
            z16 = depth.astype(np.uint16).reshape(HEIGHT, WIDTH)
            Image.fromarray(z16, mode="I;16").save(
                os.path.join(args.out, "scene%03d_z.png" % n))
        if args.depth_view:
            z = depth.astype(np.float32)
            lo, hi = float(z.min()), float(z.max())
            norm = (z - lo) * (255.0 / (hi - lo)) if hi > lo else z * 0
            Image.fromarray(norm.astype(np.uint8).reshape(HEIGHT, WIDTH)).save(
                os.path.join(args.out, "scene%03d_zview.png" % n))
        if args.raw:
            open(os.path.join(args.out, "scene%03d.bin" % n), "wb").write(
                data.tobytes())
        if args.camera:
            w, l = camera(slot[FOOTER:FOOTER + 48])
            print("scene %3d camera: %s" % (n, " ".join("%6d" % x for x in w)))
        if (n - first) % 50 == 0:
            print("scene %d/%d" % (n, last), file=sys.stderr)

    print("wrote %d scenes to %s" % (last - first + 1, args.out))


if __name__ == "__main__":
    main()
