"""track - a track as jaguarkit extracts it, from where this port's offsets begin.

`python -m jaguarkit.disc IMAGE --extract DIR` writes every data track whole,
from its block 0: 64 bytes of ATRI lead-in, the 32-byte ATARI APPROVED DATA
HEADER, a 64-byte sync pad (on track 2, where there is none, the boot image's
first 64 bytes), and then the data. Every offset in this port's tools, docs
and manifest was measured from past all three, which is where jcdinfo.py,
the tool the kit replaced, began its files. So a track is read from ORIGIN,
and the bytes and their offsets are the ones the docs give.

The engine does the same in C: src/util/io.h, TRACK_ORIGIN. The films are the
exception. A script names a film by block, and blocks count from block 0, so
the film readers take the whole file (docs/17-jaguarkit.md).
"""

ORIGIN = 160
LEAD_IN = b"ATRI" * 16
HEADER = b"ATARI APPROVED DATA HEADER ATRI"


def check(head, path="track"):
    if head[:64] != LEAD_IN or head[64:64 + len(HEADER)] != HEADER:
        raise ValueError(
            "%s is not a track as jaguarkit extracts it (no ATRI lead-in and "
            "header); extract the disc with python -m jaguarkit.disc IMAGE "
            "--extract assets/kit_tracks" % path)


def read(path):
    """The whole track from ORIGIN."""
    with open(path, "rb") as f:
        b = f.read()
    check(b, path)
    return b[ORIGIN:]


def open_at(path):
    """The track as a file positioned at ORIGIN, for readers that seek
    through a large track: seek(ORIGIN + off) for a port offset off."""
    f = open(path, "rb")
    check(f.read(ORIGIN), path)
    return f
