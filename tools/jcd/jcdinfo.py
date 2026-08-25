#!/usr/bin/env python3
"""
jcdinfo - reader for the .jcd container (Jaguar CD disc images).

Usage:
    python jcdinfo.py <image.jcd>                 list the tracks
    python jcdinfo.py <image.jcd> --extract DIR   extract de-swapped tracks
    python jcdinfo.py <image.jcd> --hex N OFF LEN hex dump of track N

The format is documented in docs/06-jcd-format.md.
No game data is included in this repository: you need a copy of a disc
image that you legitimately own.
"""

import argparse
import os
import struct
import sys

SECTOR = 2352          # block size declared by the TOC
OFFSET_UNIT = 512      # track offsets in the header are in units of 512 bytes
LEADIN = b"ATRI"       # tag repeated at the head of every data track
TRACK_HDR = b"ATARI APPROVED DATA HEADER ATRI"
HDR_LEN = 96           # track header, after the lead-in


def swap32(buf: bytes) -> bytes:
    """Track data is stored with every long byte-reversed."""
    n = len(buf) & ~3
    out = bytearray(buf)
    out[:n] = b"".join(buf[i:i + 4][::-1] for i in range(0, n, 4))
    return bytes(out)


def msf_to_blocks(b: bytes) -> int:
    return b[0] * 60 * 75 + b[1] * 75 + b[2]


class Track:
    def __init__(self, rec: bytes):
        self.no = rec[0]
        self.start = msf_to_blocks(rec[1:4])
        self.flag = rec[4]
        self.blocks = msf_to_blocks(rec[5:8])
        self.file_off = struct.unpack(">I", rec[8:12])[0] * OFFSET_UNIT
        self.kind = None       # 4-character tag, e.g. PICT / 1111
        self.type_code = None  # type byte, 0x20 + data track index
        self.payload = None    # offset of the first useful byte

    @property
    def is_audio(self) -> bool:
        return self.flag == 0

    def probe(self, fh):
        """Read the lead-in and header to determine the data track type."""
        if self.is_audio:
            self.payload = self.file_off
            return
        fh.seek(self.file_off)
        raw = swap32(fh.read(4096))
        n = 0
        while raw[n * 4:n * 4 + 4] == LEADIN:
            n += 1
        base = n * 4
        if raw[base:base + len(TRACK_HDR)] != TRACK_HDR:
            self.payload = self.file_off + base
            return
        self.type_code = raw[base + 31]
        tag = raw[base + 32:base + 36]
        self.kind = tag.decode("latin1") if all(32 <= c < 127 for c in tag) else tag.hex()
        self.payload = self.file_off + base + HDR_LEN


def read_toc(fh):
    fh.seek(0)
    head = fh.read(12)
    if head[:4] != b"JCD\0":
        raise SystemExit("not a .jcd container (magic missing)")
    ntracks = head[7]
    recs = fh.read(12 * ntracks)
    tracks = [Track(recs[i * 12:(i + 1) * 12]) for i in range(ntracks)]
    for t in tracks:
        t.probe(fh)
    return head, tracks


def cmd_list(path):
    with open(path, "rb") as fh:
        head, tracks = read_toc(fh)
        size = os.path.getsize(path)
        print(f"{path}  ({size} bytes)")
        print(f"header: {head[:4]!r} fields {head[4:12].hex(' ')}  tracks={head[7]}")
        print()
        print(f"{'tr':>3} {'type':>6} {'code':>5} {'blocks':>8} {'MB':>7} "
              f"{'start MSF':>10} {'file offset':>12} {'payload':>12}")
        for t in tracks:
            kind = "AUDIO" if t.is_audio else (t.kind or "?")
            code = "-" if t.type_code is None else f"{t.type_code:#04x}"
            mm, rem = divmod(t.start, 60 * 75)
            ss, ff = divmod(rem, 75)
            print(f"{t.no:3d} {kind:>6} {code:>5} {t.blocks:8d} "
                  f"{t.blocks * SECTOR / 1048576:7.1f} {mm:02d}:{ss:02d}:{ff:02d}   "
                  f"{t.file_off:12d} {t.payload:12d}")
        print()
        print("The 'code' column is 0x20 + data track index: it is the data type")
        print("the game uses as a track offset (see GetTrack in CDCONTRO.GAS).")


def cmd_extract(path, outdir):
    os.makedirs(outdir, exist_ok=True)
    with open(path, "rb") as fh:
        _, tracks = read_toc(fh)
        for t in tracks:
            kind = "audio" if t.is_audio else (t.kind or "data").strip().lower()
            name = os.path.join(outdir, f"track{t.no:02d}_{kind}.bin")
            todo = t.blocks * SECTOR
            fh.seek(t.payload)
            with open(name, "wb") as out:
                while todo > 0:
                    chunk = fh.read(min(1 << 22, todo))
                    if not chunk:
                        break
                    out.write(chunk if t.is_audio else swap32(chunk))
                    todo -= len(chunk)
            print(f"  {name}  ({os.path.getsize(name)} bytes)")


def cmd_hex(path, tno, off, length):
    with open(path, "rb") as fh:
        _, tracks = read_toc(fh)
        t = next(x for x in tracks if x.no == tno)
        fh.seek(t.payload + off)
        data = fh.read(length)
        if not t.is_audio:
            data = swap32(data)
        for i in range(0, len(data), 32):
            row = data[i:i + 32]
            asc = "".join(chr(c) if 32 <= c < 127 else "." for c in row)
            print(f"{off + i:#010x}  {row.hex(' ')}  |{asc}|")


def main():
    ap = argparse.ArgumentParser(description="Jaguar CD .jcd container reader")
    ap.add_argument("image")
    ap.add_argument("--extract", metavar="DIR")
    ap.add_argument("--hex", nargs=3, metavar=("TRACK", "OFF", "LEN"))
    a = ap.parse_args()
    if a.extract:
        cmd_extract(a.image, a.extract)
    elif a.hex:
        cmd_hex(a.image, int(a.hex[0]), int(a.hex[1], 0), int(a.hex[2], 0))
    else:
        cmd_list(a.image)


if __name__ == "__main__":
    sys.exit(main())
