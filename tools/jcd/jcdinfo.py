#!/usr/bin/env python3
"""
jcdinfo — lettore del container .jcd (immagini Jaguar CD).

Uso:
    python jcdinfo.py <immagine.jcd>                 elenca le tracce
    python jcdinfo.py <immagine.jcd> --extract DIR   estrae le tracce deswappate
    python jcdinfo.py <immagine.jcd> --hex N OFF LEN dump esadecimale della traccia N

Il formato e' documentato in docs/06-formato-jcd.md.
Nessun dato del gioco e' incluso in questo repository: serve una copia
dell'immagine di cui si e' legittimi proprietari.
"""

import argparse
import os
import struct
import sys

SECTOR = 2352          # dimensione di blocco dichiarata dalla TOC
OFFSET_UNIT = 512      # l'offset di traccia nell'header e' in unita' da 512 byte
LEADIN = b"ATRI"       # tag ripetuto in testa a ogni traccia dati
TRACK_HDR = b"ATARI APPROVED DATA HEADER ATRI"
HDR_LEN = 96           # header di traccia, dopo il lead-in


def swap32(buf: bytes) -> bytes:
    """I dati delle tracce sono memorizzati con ogni long a byte invertiti."""
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
        self.kind = None       # tag a 4 caratteri, es. PICT / 1111
        self.type_code = None  # byte di tipo, 0x20 + indice traccia dati
        self.payload = None    # offset del primo byte utile

    @property
    def is_audio(self) -> bool:
        return self.flag == 0

    def probe(self, fh):
        """Legge il lead-in e l'header per capire il tipo di traccia dati."""
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
        raise SystemExit("non e' un container .jcd (magic mancante)")
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
        print(f"{path}  ({size} byte)")
        print(f"header: {head[:4]!r} campi {head[4:12].hex(' ')}  tracce={head[7]}")
        print()
        print(f"{'tr':>3} {'tipo':>6} {'cod':>5} {'blocchi':>8} {'MB':>7} "
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
        print("Il 'cod' vale 0x20 + indice della traccia dati: e' il data type")
        print("usato dal gioco come offset di traccia (vedi GetTrack in CDCONTRO.GAS).")


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
            print(f"  {name}  ({os.path.getsize(name)} byte)")


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
    ap = argparse.ArgumentParser(description="lettore del container Jaguar CD .jcd")
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
