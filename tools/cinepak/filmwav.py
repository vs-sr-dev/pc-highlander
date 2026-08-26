#!/usr/bin/env python3
"""filmwav - pull the audio out of the Cinepak films.

There is no speech track on the retail disc: the dialogue is interleaved with
the video, chunk by chunk, and the `STAB` sample table in each chunk says where.
An entry is sixteen bytes - offset, size, timestamp, duration - and it is the
**timestamp** that says which kind it is:

* an **audio block** carries a timestamp of all ones.  Here `offset` is the
  **end**, so the block is `[offset - size, offset)`.  Every one on this disc
  is 16,696 bytes.
* anything else is a **video frame**: `offset` is where it starts, the
  timestamp counts the film's own ticks, and bit 31 is clear on the frames a
  player may start at.  The fourth field is then how long the frame is held -
  50 ticks at a rate of 600, which is what made it look like a type.

Both offsets are measured from the first byte after the sample table.

The audio is **signed 8-bit mono PCM**.  The rate is `audio_in` in
`CINEPAK.INC`, 22,252 Hz, and the disc bears that out: over the long films the
total audio divided by the running time comes to 21,700 to 22,000 bytes a
second, converging on 22,252 as the film gets longer.  (Short films read low
because the prefetch at the head and the missing block on the tail chunk weigh
more.)  A chunk normally carries one audio block per second of video, and every
third one carries two - 16,696 * 4/3 = 22,261, which is the rate.

`CTAB`'s `rate` field is the film's tick rate: 600 for most, but films 5 and 6
use 24 and 30.  It does not affect the audio.

Usage
    python tools/cinepak/filmwav.py TRACK7 --out assets/filmaudio
    python tools/cinepak/filmwav.py TRACK7 --film 9 --out assets/filmaudio
"""

import argparse
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from filmls import films, describe          # noqa: E402

RATE = 22252                                # audio_in, CINEPAK.INC
AUDIO_TS = -1                               # all ones, and only audio has it


def film_audio(d, o):
    """Concatenate one film's audio blocks, in order.  Returns (bytes, ticks)."""
    info = describe(d, o)
    flen = struct.unpack_from(">I", d, o + 4)[0]
    k = d.find(b"CTAB", o, o + flen + 64)
    n = info["chunks"]
    out = bytearray()
    ticks = 0
    for i in range(n):
        co, cs, cts, _ = struct.unpack_from(">IIII", d, k + 16 + i * 16)
        ticks = max(ticks, cts)
        stab = o + flen + co + 64           # past the chunk's 64-byte sync pad
        tag, size, rate, count = struct.unpack_from(">4sIII", d, stab)
        if tag != b"STAB":
            sys.stderr.write("film at $%X chunk %d: no STAB\n" % (o, i))
            break
        data = stab + size
        for j in range(count):
            eo, es, ets, et = struct.unpack_from(">IIiI", d, stab + 16 + j * 16)
            if ets == AUDIO_TS:
                out += d[data + eo - es:data + eo]
    return bytes(out), ticks, info


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
    ap.add_argument("--film", type=int)
    ap.add_argument("--rate", type=int, default=RATE)
    a = ap.parse_args()

    d = open(a.track, "rb").read()
    offs = films(d)
    if a.out:
        os.makedirs(a.out, exist_ok=True)

    total = 0.0
    for n, o in enumerate(offs):
        if a.film is not None and n != a.film:
            continue
        pcm, ticks, info = film_audio(d, o)
        secs = len(pcm) / float(a.rate)
        total += secs
        print("film %2d  block %6d  %3d chunks  %9d audio bytes  %7.2f s"
              % (n, info["block"], info["chunks"], len(pcm), secs))
        if a.out and pcm:
            sg = [c - 256 if c > 127 else c for c in pcm]
            p = os.path.join(a.out, "film%02d.wav" % n)
            open(p, "wb").write(wav(sg, a.rate))
    print("%.1f seconds of film audio at %d Hz" % (total, a.rate))


if __name__ == "__main__":
    main()
