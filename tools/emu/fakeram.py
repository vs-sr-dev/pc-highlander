#!/usr/bin/env python3
"""Build a fake 2 MB Jaguar RAM with a known picture in it, to test findfb.py.

The point of a detector that is told nothing is that it can be checked against
a dump whose answer is already known.  This plants one of our own decoded
frames at a chosen address, in the Jaguar's own RGB16 and, optionally, in
CINEPAK.S's phrase-interleaved double buffer, and surrounds it with noise.

  python tools/emu/fakeram.py build/emu/f34_200.ppm build/emu/fake.bin \\
      --base 0xC0000 --interleave
"""

import argparse

import numpy as np


def ppm_read(path):
    with open(path, "rb") as f:
        assert f.readline().strip() == b"P6"
        w, h = (int(v) for v in f.readline().split())
        assert f.readline().strip() == b"255"
        return w, h, np.frombuffer(f.read(w * h * 3), dtype=np.uint8).reshape(h, w, 3)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("ppm")
    ap.add_argument("out")
    ap.add_argument("--base", type=lambda s: int(s, 0), default=0xC0000)
    ap.add_argument("--interleave", action="store_true")
    ap.add_argument("--size", type=lambda s: int(s, 0), default=0x200000)
    args = ap.parse_args()

    w, h, rgb = ppm_read(args.ppm)
    # R5 B5 G6, the Jaguar's own order - COLLECT.GAS's "RBG = 5:5:6".
    px = (((rgb[:, :, 0].astype(np.uint16) >> 3) << 11) |
          ((rgb[:, :, 2].astype(np.uint16) >> 3) << 6) |
          (rgb[:, :, 1].astype(np.uint16) >> 2))
    rows = px.astype(">u2").tobytes()
    rows = np.frombuffer(rows, dtype=np.uint8).reshape(h, w * 2)

    rng = np.random.default_rng(7)
    ram = rng.integers(0, 256, size=args.size, dtype=np.uint8)

    stride = w * 2 * (2 if args.interleave else 1)
    for y in range(h):
        at = args.base + y * stride
        if args.interleave:
            # Both buffers, not one in noise.  The dumps say the two hold the
            # same picture except where the newest frame has changed
            # something, so a fake with rubbish in the other half is not the
            # thing being detected - and it made the self-test look worse than
            # the real dumps, which is the wrong way round for a self-test.
            src = rows[y].reshape(-1, 8)
            for p in range(src.shape[0]):
                ram[at + p * 16:at + p * 16 + 8] = src[p]
                ram[at + p * 16 + 8:at + p * 16 + 16] = src[p]
        else:
            ram[at:at + w * 2] = rows[y]

    ram.tofile(args.out)
    print("wrote %s: %dx%d at $%06X, stride %d" % (args.out, w, h, args.base, stride))


if __name__ == "__main__":
    main()
