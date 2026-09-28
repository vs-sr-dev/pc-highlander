"""jaguarkit.bigpemu.fakeram - a fake Jaguar RAM with a known picture in it.

A detector that is told nothing has to be checked against a dump whose answer
is known.  This plants a picture (a PPM - one of your own decoded frames, say)
at a chosen address in RGB16, plain or phrase-interleaved with a copy of
itself, in 2 MB of noise.  findfb has to come back with that address.

    python -m jaguarkit.bigpemu.fakeram frame.ppm fake.bin --base 0xC0000 --interleave
    python -m jaguarkit.bigpemu.findfb fake.bin --width 320 --height 240

Needs numpy.
"""

import argparse
import sys

import numpy as np

from ..pixels import read_ppm


def main(argv=None):
    ap = argparse.ArgumentParser(prog="python -m jaguarkit.bigpemu.fakeram")
    ap.add_argument("ppm")
    ap.add_argument("out")
    ap.add_argument("--base", type=lambda s: int(s, 0), default=0xC0000)
    ap.add_argument("--interleave", action="store_true")
    ap.add_argument("--size", type=lambda s: int(s, 0), default=0x200000)
    ap.add_argument("--seed", type=int, default=7)
    a = ap.parse_args(argv)

    w, h, raw = read_ppm(a.ppm)
    rgb = np.frombuffer(raw, dtype=np.uint8).reshape(h, w, 3).astype(np.uint16)
    # R5 B5 G6, rounded as jaguarkit.pixels does.
    r = np.minimum((rgb[:, :, 0] + 4) >> 3, 31)
    b = np.minimum((rgb[:, :, 2] + 4) >> 3, 31)
    g = np.minimum((rgb[:, :, 1] + 2) >> 2, 63)
    px = (r << 11) | (b << 6) | g
    rows = np.frombuffer(px.astype(">u2").tobytes(), dtype=np.uint8).reshape(h, w * 2)

    ram = np.random.default_rng(a.seed).integers(0, 256, size=a.size, dtype=np.uint8)
    stride = w * 2 * (2 if a.interleave else 1)
    for y in range(h):
        at = a.base + y * stride
        if a.interleave:
            # Both buffers hold the picture: a double buffer shows the same
            # frame in both halves except where the newest one changed it.
            src = rows[y].reshape(-1, 8)
            for p in range(src.shape[0]):
                ram[at + p * 16:at + p * 16 + 8] = src[p]
                ram[at + p * 16 + 8:at + p * 16 + 16] = src[p]
        else:
            ram[at:at + w * 2] = rows[y]
    ram.tofile(a.out)
    print("wrote %s: %dx%d at $%06X, stride %d" % (a.out, w, h, a.base, stride))
    return 0


if __name__ == "__main__":
    sys.exit(main())
