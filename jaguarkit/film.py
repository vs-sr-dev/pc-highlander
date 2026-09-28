"""jaguarkit.film - the Cinepak film container of Jaguar CD games.

Jaguar CD games that play Cinepak use one container, the one Atari's Cinepak
player reads, whatever the game.  Highlander, Battle Morph, Baldies and Myst
- 233 films - agree on every structure below; what differs between them is
marked, and is a parameter here rather than a constant.

A film is a header and then chunks of about a second each:

    'FILM' size 0 0          size is the whole header
    'FDSC' 20 codec h w      'cvid'; 320x240 in Highlander, 320x288 in
                             Battle Morph, anything from 84x106 in Myst -
                             not always a multiple of four (see cvid)
    'ADSC' 20 a b c          only in some films; not used here.  What it
                             looks like: b behaves as a clock divider - 18
                             on films whose audio runs at ~22,250 bytes a
                             second, 37 on one at 11,126, and 22,252 x 19 =
                             11,126 x 38 - and a is 1 on the films with twice
                             the bytes.  A reading of three discs, unproven
    'CTAB' size rate count   then 16 bytes per chunk: offset, size,
                             timestamp, tag.  rate is ticks per second

Every chunk opens with a 64-byte sync pad - its tag, sixteen times - and then

    'STAB' size rate count   then 16 bytes per sample: offset, size,
                             timestamp, duration

which interleaves video frames with audio.  Chunk offsets count from the end
of the film header; sample offsets from the end of the sample table.  The
timestamp says what a sample is:

* all ones: an **audio block**.  Its offset is where it **ends** in films
  whose blocks are 16,696 bytes (Highlander's, most of Battle Morph's) and
  where it **starts** in films whose blocks are 16,384 (Battle Morph's three
  without an ADSC).  Nothing in the header says which, but the samples tile
  the chunk in table order on every disc, so an audio block begins where the
  sample before it ended - and that is how this reads it, checking that one
  of the two readings fits.
* anything else: a **video frame**, in the film's ticks; bit 31 is clear on
  exactly the frames a player may start on (a whole picture, or the first
  frame after an audio block - all 13,922 of Highlander's).  The fourth field
  is how long the frame is held: 50 at a rate of 600 is 12 fps, 2 at 30 is 15.

The audio is 8-bit PCM, **signed in Highlander, Baldies and Myst and unsigned
in Battle Morph** - the bytes sit around $00 in the first three and around
$80 in the last - and the container does not say which, so `--wav` asks.  The
rate is the player's (Highlander's CINEPAK.INC says 22,252 Hz); the listing
measures audio bytes per second of film, which converges on it for long films.

A film does not have to start on a block boundary: the player seeks to a
block and scans forward for the pad.  So films are found here by structure -
'FILM' with 'FDSC' sixteen bytes on - and a film's block number is the block
its sync pad starts in, counted from the start of the track (its lead-in),
which is the number a game's script names: all 36 of Highlander's.

Usage
    python -m jaguarkit.film IMAGE --track 7                 the films
    python -m jaguarkit.film TRACK.bin                       an extracted track
    python -m jaguarkit.film IMAGE --track 7 --chunks 3      film 3's chunk table
    python -m jaguarkit.film IMAGE --track 7 --wav DIR --pcm s8 --rate 22252
    python -m jaguarkit.film IMAGE --track 7 --film 3 --frame 30 --ppm f.ppm
    python -m jaguarkit.film IMAGE --track 7 --check         decode every frame
"""

import argparse
import os
import struct
import sys

BLOCK = 2352
SYNC = 64
AUDIO_TS = 0xFFFFFFFF


class Film:
    """One film: where it is, its header blocks and its chunk table."""

    def __init__(self, data, offset):
        self.d = data
        self.offset = offset
        self.header = struct.unpack_from(">I", data, offset + 4)[0]
        self.codec = None
        self.width = self.height = 0
        self.adsc = None            # raw, when present
        self.rate = 0
        self.chunks = []            # (offset, size, timestamp, tag)
        p = offset + 16
        while p + 8 <= offset + self.header:
            tag = data[p:p + 4]
            size = struct.unpack_from(">I", data, p + 4)[0]
            if size < 8:
                break
            if tag == b"FDSC":
                c, h, w = struct.unpack_from(">4sII", data, p + 8)
                self.codec, self.height, self.width = c.decode("latin1"), h, w
            elif tag == b"ADSC":
                self.adsc = bytes(data[p + 8:p + size])
            elif tag == b"CTAB":
                self.rate, n = struct.unpack_from(">II", data, p + 8)
                self.chunks = [struct.unpack_from(">IIII", data, p + 16 + i * 16)
                               for i in range(n)]
            p += size

    @property
    def block(self):
        """The block a game names to play it: where its sync pad starts, if
        one is in front of it (Highlander's film 7 has its pad in block
        16,782 and its header in 16,783, and the script says 16,782)."""
        pad = self.d[self.offset - SYNC:self.offset]
        if self.offset >= SYNC and pad == pad[:4] * 16:
            return (self.offset - SYNC) // BLOCK
        return self.offset // BLOCK

    @property
    def payload(self):
        return self.offset + self.header

    @property
    def bytes(self):
        if not self.chunks:
            return 0
        o, s, _, _ = self.chunks[-1]
        return o + s

    @property
    def ticks(self):
        return self.chunks[-1][2] if self.chunks else 0

    def samples(self):
        """Every sample, in order: (audio?, offset in data, size, ts, dur)."""
        d = self.d
        for (co, cs, cts, tag) in self.chunks:
            stab = self.payload + co + SYNC
            magic, size, rate, count = struct.unpack_from(">4sIII", d, stab)
            if magic != b"STAB":
                raise ValueError("chunk at $%X has no STAB" % (self.payload + co))
            base = stab + size
            prev = 0                        # where the last sample ended
            for j in range(count):
                eo, es, ets, dur = struct.unpack_from(">IIII", d, stab + 16 + j * 16)
                audio = ets == AUDIO_TS
                at = eo
                if audio:
                    if eo - es == prev:     # the offset is its end
                        at = eo - es
                    elif eo != prev:        # neither reading tiles
                        raise ValueError("chunk at $%X: audio block of %d "
                                         "bytes at %d, the last sample ended "
                                         "at %d" % (self.payload + co, es, eo,
                                                    prev))
                prev = at + es
                yield (audio, base + at, es, ets, dur)

    def video(self):
        return [(p, s, ts, dur) for (a, p, s, ts, dur) in self.samples() if not a]

    def audio(self):
        """The audio blocks, concatenated in order, as stored."""
        return b"".join(bytes(self.d[p:p + s])
                        for (a, p, s, _, _) in self.samples() if a)

    def seconds(self):
        """Running time, from the last video frame's end."""
        v = self.video()
        if not v or not self.rate:
            return 0.0
        _, _, ts, dur = v[-1]
        return ((ts & 0x7FFFFFFF) + dur) / float(self.rate)


def scan(data):
    """Every film in a buffer: 'FILM' with 'FDSC' sixteen bytes on.  'FILM'
    also turns up inside video, so the check is structural."""
    out = []
    o = 0
    while True:
        o = data.find(b"FILM", o)
        if o < 0:
            return out
        size = struct.unpack_from(">I", data, o + 4)[0] if o + 8 <= len(data) else 0
        if 16 < size < 0x10000 and data[o + 16:o + 20] == b"FDSC":
            f = Film(data, o)
            if f.chunks:
                out.append(f)
                o += max(f.header + f.bytes, 4)
                continue
        o += 4


def wav(pcm, rate, signed):
    """8-bit mono PCM in, an unsigned 8-bit RIFF file out."""
    if signed:
        pcm = bytes((c + 128) & 0xFF for c in pcm)
    n = len(pcm)
    return (b"RIFF" + struct.pack("<I", 36 + n) + b"WAVEfmt " +
            struct.pack("<IHHIIHH", 16, 1, 1, rate, rate, 1, 8) +
            b"data" + struct.pack("<I", n) + pcm)


def load(path, track):
    """A track's bytes from block 0: from an image and a track number, or an
    extracted file (which is then taken to start at block 0)."""
    if track is None:
        return open(path, "rb").read()
    from .disc import Disc
    t = Disc(path).track(track)
    return t.read(0, t.size)


def main(argv=None):
    ap = argparse.ArgumentParser(prog="python -m jaguarkit.film")
    ap.add_argument("source", help="a disc image (with --track) or a track file")
    ap.add_argument("--track", type=int)
    ap.add_argument("--chunks", type=int, metavar="N")
    ap.add_argument("--wav", metavar="DIR")
    ap.add_argument("--pcm", choices=("s8", "u8"),
                    help="the audio's sign: s8 in Highlander, u8 in Battle Morph")
    ap.add_argument("--rate", type=int, help="Hz; default: measured")
    ap.add_argument("--film", type=int)
    ap.add_argument("--frame", type=int, default=0)
    ap.add_argument("--ppm")
    ap.add_argument("--check", action="store_true")
    a = ap.parse_args(argv)

    d = load(a.source, a.track)
    films = scan(d)
    pick = range(len(films)) if a.film is None else [a.film]

    if a.chunks is not None:
        f = films[a.chunks]
        print("film %d: %d chunks, rate %d" % (a.chunks, len(f.chunks), f.rate))
        for j, (co, cs, ts, tag) in enumerate(f.chunks):
            print("  %3d  offset $%08X  size %8d  t %7d  tag %r"
                  % (j, co, cs, ts, struct.pack(">I", tag)))
        return 0

    if a.wav:
        if not a.pcm:
            sys.exit("--wav needs --pcm s8 or u8: the container does not say")
        os.makedirs(a.wav, exist_ok=True)
        for n in pick:
            f = films[n]
            pcm = f.audio()
            secs = f.seconds()
            rate = a.rate or (int(round(len(pcm) / secs)) if secs else 22050)
            path = os.path.join(a.wav, "film%02d.wav" % n)
            open(path, "wb").write(wav(pcm, rate, a.pcm == "s8"))
            print("%s  %d bytes at %d Hz" % (path, len(pcm), rate))
        return 0

    if a.ppm or a.check:
        from .cvid import Decoder
        from .pixels import write_ppm
        bad = 0
        for n in pick:
            f = films[n]
            dec = Decoder(f.width, f.height)
            vid = f.video()
            last = len(vid) if (a.check or a.film is None) else \
                min(a.frame + 1, len(vid))
            for i in range(last):
                p, sz, ts, dur = vid[i]
                try:
                    dec.frame(d, p, sz)
                except ValueError as e:
                    print("film %d frame %d: %s" % (n, i, e))
                    bad += 1
            print("film %2d  %dx%d  %d frames decoded, %d whole"
                  % (n, f.width, f.height, dec.frames, dec.keyframes))
            if a.ppm:
                write_ppm(a.ppm, f.width, f.height, dec.picture())
        return 1 if bad else 0

    print("%4s %7s %11s %6s %6s %5s %4s %7s %8s %9s %s" %
          ("film", "block", "offset", "codec", "size", "rate", "fps",
           "chunks", "seconds", "audio B/s", "adsc"))
    for n in pick:
        f = films[n]
        v = f.video()
        fps = (f.rate / float(v[1][3])) if len(v) > 1 and v[1][3] else 0
        secs = f.seconds()
        abytes = sum(s for (au, _, s, _, _) in f.samples() if au)
        print("%4d %7d %11d %6s %3dx%-3d %4d %4.1f %7d %8.1f %9.0f %s" %
              (n, f.block, f.offset, f.codec, f.width, f.height, f.rate, fps,
               len(f.chunks), secs, abytes / secs if secs else 0,
               f.adsc.hex() if f.adsc else "-"))
    print("%d films" % len(films))
    return 0


if __name__ == "__main__":
    sys.exit(main())
