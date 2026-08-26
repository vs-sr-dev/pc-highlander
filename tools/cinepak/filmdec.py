#!/usr/bin/env python3
"""filmdec - decode the Cinepak video inside the films.

`filmls` reads the container and `filmwav` pulls the audio out of it; what is
left is the picture, and the picture is plain `cvid` - the Cinepak the rest of
the world has, not something Jaguar-shaped.  So this is written from the
published format rather than from the disc's own hand-written decoder, which is
what docs/05-roadmap.md 5.2 asks for.

A frame is ten bytes of header and then strips:

    flags(1) length(3) width(2) height(2) strips(2)
    strip:  id(2) size(2) y0(2) x0(2) y1(2) x1(2)

Strips stack down the picture - both of this disc's are 320x120 - and each one
carries its own pair of codebooks plus the vectors that index them:

    $20 / $21   V4 codebook, whole / updated under a flag word
    $22 / $23   V1 codebook, likewise
    $30         vectors, one flag bit per block: V1 or V4
    $31         vectors, one flag bit for "coded at all", then the V1/V4 bit
    $32         vectors, V1 throughout

A codebook entry is four Y and a signed U and V, and it paints a 2x2: V1 uses
one entry doubled over the whole 4x4 block, V4 uses four, one per quadrant.
The colour is Cinepak's own, R = Y + 2V, G = Y - U/2 - V, B = Y + 2U.

An inter frame leaves the blocks it does not code alone, so the decoder keeps
the last picture; a strip with no codebook of its own inherits the one before
it, which is how this disc's second strip is usually empty.

Usage
    python tools/cinepak/filmdec.py TRACK7 --frames          # frames per film
    python tools/cinepak/filmdec.py TRACK7 --film 19 --frame 0 --ppm f.ppm
    python tools/cinepak/filmdec.py TRACK7 --film 19 --check
"""

import argparse
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from filmls import films, describe          # noqa: E402

AUDIO_TS = 0xFFFFFFFF       # what marks an audio block, rather than a type
MAX_STRIPS = 32


def samples(d, o):
    """Every sample of one film, in order, as (audio?, offset, size, ts, dur).

    The fourth field of a `STAB` entry is a *duration*, not a type: 50 ticks at
    a rate of 600, 2 at 24, 2 and 3 alternating at 30 - all of them 12 fps.  It
    is the timestamp that says which is which, all ones marking audio.
    """
    info = describe(d, o)
    flen = struct.unpack_from(">I", d, o + 4)[0]
    for (co, cs, cts, tag) in info["entries"]:
        stab = o + flen + co + 64           # past the chunk's 64-byte sync pad
        magic, size, rate, count = struct.unpack_from(">4sIII", d, stab)
        if magic != b"STAB":
            raise ValueError("chunk at 0x%x has no STAB" % (o + flen + co))
        data = stab + size
        for j in range(count):
            eo, es, ets, et = struct.unpack_from(">IIII", d, stab + 16 + j * 16)
            audio = ets == AUDIO_TS
            yield (audio, data + (eo - es if audio else eo), es, ets, et)


def video(d, o):
    return [(p, sz, ts, dur)
            for (a, p, sz, ts, dur) in samples(d, o) if not a]


def clip(v):
    return 0 if v < 0 else (255 if v > 255 else v)


class Codebook:
    """256 entries of four RGB triples - one 2x2 block, already converted."""

    def __init__(self):
        self.e = [bytearray(12) for _ in range(256)]

    def load(self, cb):
        for i in range(256):
            self.e[i][:] = cb.e[i]

    def read(self, d, p, size, update, grey):
        end = p + size
        n = 4 if grey else 6
        flag = 0
        mask = 0
        for i in range(256):
            if update:
                mask >>= 1
                if not mask:
                    if p + 4 > end:
                        return
                    flag = struct.unpack_from(">I", d, p)[0]
                    p += 4
                    mask = 0x80000000
                if not flag & mask:
                    continue
            if p + n > end:
                return
            y = d[p:p + 4]
            p += 4
            if grey:
                u = v = 0
            else:
                u, v = struct.unpack_from(">bb", d, p)
                p += 2
            e = self.e[i]
            for k in range(4):
                e[k * 3 + 0] = clip(y[k] + 2 * v)
                e[k * 3 + 1] = clip(y[k] - (u >> 1) - v)
                e[k * 3 + 2] = clip(y[k] + 2 * u)


class Decoder:
    def __init__(self, w, h):
        self.w, self.h = w, h
        self.rgb = bytearray(w * h * 3)
        self.v1 = [Codebook() for _ in range(MAX_STRIPS)]
        self.v4 = [Codebook() for _ in range(MAX_STRIPS)]
        self.frames = self.keyframes = 0

    # -- painting ----------------------------------------------------
    def blk4(self, x, y, cb, i0, i1, i2, i3):
        """Four codebook entries, one per quadrant of a 4x4 block."""
        w, rgb = self.w, self.rgb
        for q, idx in enumerate((i0, i1, i2, i3)):
            e = cb.e[idx]
            bx, by = x + (q & 1) * 2, y + (q >> 1) * 2
            for r in range(2):
                o = ((by + r) * w + bx) * 3
                rgb[o:o + 6] = e[r * 6:r * 6 + 6]

    def blk1(self, x, y, cb, i):
        """One entry, each of its four pixels doubled both ways."""
        w, rgb = self.w, self.rgb
        e = cb.e[i]
        for r in range(4):
            o = ((y + r) * w + x) * 3
            h = (r >> 1) * 6
            a = e[h:h + 3]
            b = e[h + 3:h + 6]
            rgb[o:o + 12] = a + a + b + b

    # -- the bitstream -----------------------------------------------
    def vectors(self, d, p, size, cid, s, y0, y1):
        end = p + size
        v1, v4 = self.v1[s], self.v4[s]
        inter = cid & 0x01                  # $31: a "coded at all" flag first
        v1only = cid & 0x02                 # $32: no V1/V4 flag at all
        flag = 0
        mask = 0
        for y in range(y0, y1 - 3, 4):
            for x in range(0, self.w - 3, 4):
                if inter:
                    mask >>= 1
                    if not mask:
                        if p + 4 > end:
                            raise ValueError("vectors ran out")
                        flag = struct.unpack_from(">I", d, p)[0]
                        p += 4
                        mask = 0x80000000
                    if not flag & mask:
                        continue            # left as the last frame had it
                use_v1 = True
                if not v1only:
                    mask >>= 1
                    if not mask:
                        if p + 4 > end:
                            raise ValueError("vectors ran out")
                        flag = struct.unpack_from(">I", d, p)[0]
                        p += 4
                        mask = 0x80000000
                    use_v1 = not (flag & mask)
                if use_v1:
                    if p + 1 > end:
                        raise ValueError("vectors ran out")
                    self.blk1(x, y, v1, d[p])
                    p += 1
                else:
                    if p + 4 > end:
                        raise ValueError("vectors ran out")
                    self.blk4(x, y, v4, d[p], d[p + 1], d[p + 2], d[p + 3])
                    p += 4

    def strip(self, d, p, size, s, y0, y1):
        end = p + size
        while p + 4 <= end:
            cid, csize = struct.unpack_from(">HH", d, p)
            if csize < 4 or p + csize > end:
                raise ValueError("chunk $%04x of %d bytes overruns its strip"
                                 % (cid, csize))
            cid >>= 8
            body, blen = p + 4, csize - 4
            if cid in (0x20, 0x21, 0x24, 0x25):
                self.v4[s].read(d, body, blen, cid & 1, cid & 4)
            elif cid in (0x22, 0x23, 0x26, 0x27):
                self.v1[s].read(d, body, blen, cid & 1, cid & 4)
            elif cid in (0x30, 0x31, 0x32):
                self.vectors(d, body, blen, cid, s, y0, y1)
            p += csize

    def frame(self, d, p, size):
        if size < 10:
            raise ValueError("frame of %d bytes" % size)
        flags = d[p]
        length = struct.unpack_from(">I", d, p)[0] & 0xFFFFFF
        w, h, nstrips = struct.unpack_from(">HHH", d, p + 4)
        if length != size:
            raise ValueError("frame says %d bytes, the sample table says %d"
                             % (length, size))
        if (w, h) != (self.w, self.h):
            raise ValueError("frame is %dx%d, the film says %dx%d"
                             % (w, h, self.w, self.h))
        if nstrips > MAX_STRIPS:
            raise ValueError("%d strips" % nstrips)
        q = p + 10
        y = 0
        for s in range(nstrips):
            if q + 12 > p + size:
                raise ValueError("strip %d header past the frame" % s)
            sid, ssize = struct.unpack_from(">HH", d, q)
            height = struct.unpack_from(">H", d, q + 8)[0]
            if ssize < 12 or q + ssize > p + size:
                raise ValueError("strip %d of %d bytes overruns the frame"
                                 % (s, ssize))
            # A strip with no codebook of its own continues the one above it,
            # which is what an empty $21/$23 in the second strip means.
            if s > 0 and not (flags & 0x01):
                self.v1[s].load(self.v1[s - 1])
                self.v4[s].load(self.v4[s - 1])
            self.strip(d, q + 12, ssize - 12, s, y, min(y + height, self.h))
            y += height
            q += ssize
        self.frames += 1
        if not flags & 0x01:
            self.keyframes += 1

    def ppm(self, path):
        with open(path, "wb") as f:
            f.write(b"P6\n%d %d\n255\n" % (self.w, self.h))
            f.write(bytes(self.rgb))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("track")
    ap.add_argument("--film", type=int)
    ap.add_argument("--frame", type=int, default=0)
    ap.add_argument("--ppm")
    ap.add_argument("--frames", action="store_true",
                    help="count the video samples of every film")
    ap.add_argument("--check", action="store_true",
                    help="decode every frame, not just the one --frame names")
    a = ap.parse_args()

    d = open(a.track, "rb").read()
    offs = films(d)
    pick = range(len(offs)) if a.film is None else [a.film]

    if a.frames:
        print("film\tchunks\tframes\taudio")
        for n in pick:
            v = av = 0
            for (a, p, sz, ts, dur) in samples(d, offs[n]):
                if a:
                    av += 1
                else:
                    v += 1
            print("%d\t%d\t%d\t%d"
                  % (n, describe(d, offs[n])["chunks"], v, av))
        return 0

    bad = 0
    for n in pick:
        info = describe(d, offs[n])
        dec = Decoder(info["width"], info["height"])
        vid = video(d, offs[n])
        # Decoding stops at --frame: an inter frame only means anything with
        # every frame before it already decoded, so the run is from the start.
        want = range(len(vid)) if (a.check or a.film is None) \
            else range(min(a.frame + 1, len(vid)))
        for i in want:
            p, sz, ts, dur = vid[i]
            try:
                dec.frame(d, p, sz)
            except ValueError as e:
                print("film %d frame %d: %s" % (n, i, e))
                bad += 1
        print("film %2d  %d frames decoded, %d of them whole pictures"
              % (n, dec.frames, dec.keyframes))
        if a.ppm:
            dec.ppm(a.ppm)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
