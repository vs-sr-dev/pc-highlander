#!/usr/bin/env python3
"""wavex - extract the sampled sounds as .wav.

The sound effects sit in `WAVE` records, on the two tracks that use the 56-block
slot stride:

```
+0   .l  total      bytes from the tag to the end of the samples
+4   4   'WAVE'
+8   .l  size       number of samples
+12  ..  samples    signed 8-bit
```

`total` is `size + 8` **rounded up to a multiple of four** — the leading long is
not counted by it — so the record occupies `total + 4` bytes and the next one
starts on a long boundary. Records run one after another inside a slot; a long
that does not head a valid record ends the chain.

* **Track 6** holds one record per slot, 38 of them.
* **Track 5** is the per-entity bundle, and the odd slots hold **four records
  each** — the four combat sounds `STRUCDEF.INC` gives a character:
  `soundKIA`, `soundHIT`, `soundATT`, `soundPAR`, in that order. Slots 1 to 17
  odd carry nine characters' worth; slot 28 carries a tenth set behind other
  data, so the walk starts at the first `WAVE` tag in the slot rather than at
  its start.

**Sample rate.** `WAVE.DAS` runs the player off timer 1, programmed with
`JPIT1 = 1` and `JPIT2 = $25A`, so the fetch rate is
`26,590,906 / (2 * 603)` = **22,048 Hz** on an NTSC machine. That is written out
as 22,050 by default, which is the same thing to four digits and is what every
player expects. (`SCLK = 19` sets the I2S bit clock, not the sample rate.)

Usage
    python tools/wave/wavex.py TRACK6 --out assets/waves
    python tools/wave/wavex.py TRACK5 --out assets/waves --list
"""

import argparse
import os
import struct
import sys

SLOT = 56 * 2352
TAG = b"WAVE"


def head(d, p, end):
    """(total, size) if a valid record header starts at p, else None."""
    if p + 12 > end:
        return None
    total, tag, size = struct.unpack_from(">I4sI", d, p)
    if tag != TAG or size == 0 or p + 12 + size > end:
        return None
    if total != (size + 8 + 3) & ~3:
        return None
    return total, size


def records(d, o, end):
    """Walk the WAVE chain in one slot, starting at the first record in it."""
    p = o
    while p + 12 <= end and not head(d, p, end):
        p += 4
    while True:
        h = head(d, p, end)
        if not h:
            return
        total, size = h
        yield p - o, d[p + 12:p + 12 + size]
        p += 4 + total


def wav(samples, rate):
    """Signed 8-bit in, an unsigned-8-bit mono RIFF file out."""
    pcm = bytes((s + 128) & 0xFF for s in samples)
    n = len(pcm)
    return (b"RIFF" + struct.pack("<I", 36 + n) + b"WAVEfmt " +
            struct.pack("<IHHIIHH", 16, 1, 1, rate, rate, 1, 8) +
            b"data" + struct.pack("<I", n) + pcm)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("track")
    ap.add_argument("--out")
    ap.add_argument("--rate", type=int, default=22050)
    ap.add_argument("--list", action="store_true")
    a = ap.parse_args()

    d = open(a.track, "rb").read()
    stem = os.path.splitext(os.path.basename(a.track))[0]
    if a.out:
        os.makedirs(a.out, exist_ok=True)

    total = seconds = 0
    for n in range(len(d) // SLOT):
        o = n * SLOT
        for i, (off, s) in enumerate(records(d, o, o + SLOT)):
            sg = [c - 256 if c > 127 else c for c in s]
            total += 1
            seconds += len(s) / float(a.rate)
            if a.list or not a.out:
                print("slot %2d  #%d  off $%05X  %6d samples  %5.2f s  "
                      "peak %4d" % (n, i, off, len(s), len(s) / float(a.rate),
                                    max(abs(min(sg)), abs(max(sg)))))
            if a.out:
                p = os.path.join(a.out, "%s_s%02d_%d.wav" % (stem, n, i))
                open(p, "wb").write(wav(sg, a.rate))
    print("%d waves, %.1f seconds at %d Hz" % (total, seconds, a.rate))
    if not total:
        sys.exit("no WAVE records found - is this the right track?")


if __name__ == "__main__":
    main()
