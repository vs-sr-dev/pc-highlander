"""jaguarkit.disc - Jaguar CD disc images: .jcd, and Redump .cue/.bin.

A Jaguar CD is a multisession audio disc.  Session 1 is audio a CD player can
play - one track on most discs, the whole soundtrack on some (Iron Soldier 2
keeps eleven there) - and session 2 holds the game.  Every track in it, data
included, is written as audio sectors of 2352 bytes, with no Mode 1 framing
and no ECC.  So there is no filesystem: a game addresses its data as (track,
block) and finds its way with tags.

What every data track looks like, on every disc this has been run on:

    'ATRI' x 16                           the lead-in, 64 bytes
    'ATARI APPROVED DATA HEADER ATRI' + c  32 bytes; c is ' ' on the first
                                          data track and counts up from there
                                          (' ' '!' '"' ...), which is how a
                                          game's GetTrack tells tracks apart
    then the track's own bytes
    'ATARI APPROVED DATA TAILER ATRI' + c  32 bytes, the same c
    'ATRI' x 16
    zero to the end of the track

42 of 42 data tracks on five discs close that way.  On four of them some
high-entropy bytes follow the last 'ATRI' (674 to 4,510 of them); nothing
addresses them, and a .jcd does not keep them - see `--same-as`.

On the first data track of session 2 - the boot track - those bytes are
a load address, a length, and that many bytes of 68000 code, which the CD
BIOS copies to the address and runs.  What the other tracks hold is the
game's business; many of them open with a 64-byte sync pad of their own (a
four-byte tag sixteen times, e.g. 'DATA', 'PICT', '1111', 'CPK0'), and this
module reports that tag when it sees one.

Block 0 of a track starts at its lead-in, so a game's block number b is byte
b * 2352 of what `Track.read` returns.

The two image formats store the same big-endian stream differently:

    .jcd        every 32-bit long byte-reversed; each track stored up to its
                tailer, 512-byte aligned, in a file with a 'JCD\\0' header
                and a 12-byte TOC record per track
    .cue/.bin   Redump's: every 16-bit word byte-swapped (CD-DA's little-
                endian samples), one file per track with its pregap, and the
                data shifted a few bytes off the sector grid by the drive's
                read offset, so the lead-in is searched for, not assumed

Audio tracks come out of `Track.wav` as standard 44.1 kHz stereo WAV.  The
.jcd holds audio the way it holds data (32-bit reversed), and one 16-bit
sample out of phase with Redump's: `wav()` corrects both, and `--same-as`
checks it.

Usage
    python -m jaguarkit.disc IMAGE                      tracks, headers, boot
    python -m jaguarkit.disc IMAGE --extract DIR        every track, de-swapped
    python -m jaguarkit.disc IMAGE --extract DIR --payload   header to tailer
    python -m jaguarkit.disc IMAGE --boot FILE          the boot image alone
    python -m jaguarkit.disc IMAGE --hex TRACK OFF LEN
    python -m jaguarkit.disc A.jcd --same-as B.cue      two images, compared
"""

import argparse
import os
import re
import struct
import sys

SECTOR = 2352
LEADIN = b"ATRI"
LEADIN_LEN = 64                             # sixteen of them
HEADER = b"ATARI APPROVED DATA HEADER ATRI"  # 31 bytes, then the marker byte
HEADER_LEN = 32
TAILER = b"ATARI APPROVED DATA TAILER ATRI"
SYNC_LEN = 64                               # a game's own tag, sixteen times


def swap16(b):
    """Byte-swap every 16-bit word.  An odd trailing byte is kept."""
    n = len(b) & ~1
    out = bytearray(b)
    out[0:n:2] = b[1:n:2]
    out[1:n:2] = b[0:n:2]
    return bytes(out)


def swap32(b):
    """Byte-reverse every 32-bit long.  A ragged tail is kept as it is."""
    n = len(b) & ~3
    out = bytearray(b)
    out[0:n:4] = b[3:n:4]
    out[1:n:4] = b[2:n:4]
    out[2:n:4] = b[1:n:4]
    out[3:n:4] = b[0:n:4]
    return bytes(out)


def msf(m, s, f):
    return (m * 60 + s) * 75 + f


# ---------------------------------------------------------------------------
# sources: a byte stream per track, already de-swapped to big-endian
# ---------------------------------------------------------------------------

class _Files:
    """Several files read as one stream (a session of a .cue)."""

    def __init__(self, paths):
        self.parts = []
        at = 0
        for p in paths:
            n = os.path.getsize(p)
            self.parts.append((at, n, p))
            at += n
        self.size = at
        self._fh = {}

    def read(self, pos, n):
        out = bytearray()
        for base, size, path in self.parts:
            if n <= 0:
                break
            if pos >= base + size or pos + n <= base:
                continue
            lo = max(pos, base)
            hi = min(pos + n, base + size)
            fh = self._fh.get(path)
            if fh is None:
                fh = self._fh[path] = open(path, "rb")
            fh.seek(lo - base)
            out += fh.read(hi - lo)
        return bytes(out)


class Track:
    """One track.  `read` gives de-swapped, big-endian bytes from block 0."""

    def __init__(self, no, session, audio, lba, blocks):
        self.no = no
        self.session = session
        self.audio = audio
        self.lba = lba              # where the track starts on the disc
        self.blocks = blocks        # its length on the disc, pregap excluded
        self.size = 0               # bytes the image actually holds
        self.leadin = 0             # 'ATRI's before the header
        self.marker = None          # the header's last byte, ' ' = first
        self.tag = None             # a 64-byte sync pad's tag, if one follows
        self.boot = None            # (load address, length) on the boot track
        self.tailer = None          # offset of the tailer, if it was found
        self._read = None           # (pos, n) -> bytes, big-endian
        self._wav_shift = 0         # bytes of silence a .jcd's audio lacks

    @property
    def index(self):
        """The data track's index as its header counts it, 0 = boot track."""
        return None if self.marker is None else self.marker - 0x20

    @property
    def payload(self):
        """Offset of the first byte after the Atari header."""
        return self.leadin * 4 + HEADER_LEN

    @property
    def data(self):
        """Offset of the first byte past the header and any sync pad."""
        return self.payload + (SYNC_LEN if self.tag else 0)

    @property
    def end(self):
        """Offset one past the track's own bytes: the tailer, or the image's
        end when there is none."""
        return self.tailer if self.tailer is not None else self.size

    @property
    def closed(self):
        """Offset one past the tailer and the 'ATRI's after it."""
        if self.tailer is None:
            return self.size
        return min(self.size, self.tailer + HEADER_LEN + LEADIN_LEN)

    def last_nonzero(self):
        """Offset one past the image's last non-zero byte."""
        pos = self.size
        while pos > 0:
            lo = max(0, pos - (1 << 20))
            kept = self.read(lo, pos - lo).rstrip(b"\0")
            if kept:
                return lo + len(kept)
            pos = lo
        return 0

    def read(self, off, n):
        if off >= self.size or n <= 0:
            return b""
        return self._read(off, min(n, self.size - off))

    def boot_image(self):
        """The bytes the CD BIOS loads, and where it loads them."""
        if not self.boot:
            return None
        load, length = self.boot
        return load, self.read(self.payload + 8, length)

    def wav(self, fh):
        """An audio track as a standard 16-bit stereo 44.1 kHz WAV."""
        n = self.size + self._wav_shift
        n -= n % 4
        fh.write(b"RIFF" + struct.pack("<I", 36 + n) + b"WAVEfmt " +
                 struct.pack("<IHHIIHH", 16, 1, 2, 44100, 176400, 4, 16) +
                 b"data" + struct.pack("<I", n))
        left = n
        fh.write(bytes(self._wav_shift))
        left -= self._wav_shift
        pos = 0
        while left > 0:
            chunk = self.read(pos, min(1 << 22, left))
            if not chunk:
                break
            fh.write(swap16(chunk))
            pos += len(chunk)
            left -= len(chunk)
        if left > 0:
            fh.write(bytes(left))

    def _probe(self):
        """Read the lead-in and header of a data track."""
        if self.audio:
            return
        head = self.read(0, LEADIN_LEN + HEADER_LEN + 8 + SYNC_LEN + 4)
        n = 0
        while head[n * 4:n * 4 + 4] == LEADIN:
            n += 1
        if head[n * 4:n * 4 + len(HEADER)] != HEADER:
            return
        self.leadin = n
        self.marker = head[n * 4 + len(HEADER)]
        p = self.payload
        pad = head[p:p + SYNC_LEN]
        if len(pad) == SYNC_LEN and pad == pad[:4] * 16 and any(pad[:4]):
            self.tag = pad[:4]
        # The tailer is near the last non-zero byte, before whatever follows
        # it on the few tracks where something does.
        last = self.last_nonzero()
        lo = max(p, last - 65536)
        k = self.read(lo, last - lo).rfind(TAILER + bytes([self.marker]))
        if k >= 0:
            self.tailer = lo + k


class Disc:
    """A Jaguar CD image, .jcd or .cue."""

    def __init__(self, path):
        self.path = path
        with open(path, "rb") as fh:
            magic = fh.read(4)
        if magic == b"JCD\0":
            self.kind = "jcd"
            self._open_jcd()
        elif path.lower().endswith(".cue"):
            self.kind = "cue"
            self._open_cue()
        else:
            raise ValueError("%s: neither a .jcd nor a .cue" % path)
        for t in self.tracks:
            t._probe()
        # The boot track: the first data track of session 2, whose header
        # marker is ' '.  Its payload opens with a load address and a length.
        for t in self.tracks:
            if t.marker == 0x20:
                load, length = struct.unpack(">II", t.read(t.payload, 8))
                t.boot = (load, length)
                break

    def track(self, no):
        for t in self.tracks:
            if t.no == no:
                return t
        raise KeyError("no track %d" % no)

    # -- .jcd ---------------------------------------------------------------
    def _open_jcd(self):
        """'JCD\\0', three bytes, the track count, four more (12 in all), and
        then per track:
        number, start MSF, a flag (0 = audio), length MSF, and the file
        offset in units of 512 bytes."""
        fh = open(self.path, "rb")
        head = fh.read(12)
        ntracks = head[7]
        recs = fh.read(12 * ntracks)
        size = os.path.getsize(self.path)
        self.tracks = []
        offs = []
        for i in range(ntracks):
            r = recs[i * 12:(i + 1) * 12]
            t = Track(r[0], 0, r[4] == 0, msf(*r[1:4]), msf(*r[5:8]))
            offs.append(struct.unpack(">I", r[8:12])[0] * 512)
            self.tracks.append(t)
        # Sessions are not in the TOC; the only audio track before the first
        # data track is session 1, which is how every Jaguar CD is laid out.
        first_data = next((i for i, t in enumerate(self.tracks)
                           if not t.audio), len(self.tracks))
        for i, t in enumerate(self.tracks):
            t.session = 1 if i < max(first_data, 1) else 2
            end = offs[i + 1] if i + 1 < ntracks else size
            t.size = min(end - offs[i], t.blocks * SECTOR)
            t._read = self._jcd_reader(fh, offs[i])
            # A .jcd's audio is one 16-bit sample behind Redump's; see wav().
            t._wav_shift = 2 if t.audio else 0

    @staticmethod
    def _jcd_reader(fh, base):
        def read(pos, n):
            lo = pos & ~3
            fh.seek(base + lo)
            raw = fh.read(((pos + n + 3) & ~3) - lo)
            return swap32(raw)[pos - lo:pos - lo + n]
        return read

    # -- .cue ---------------------------------------------------------------
    def _open_cue(self):
        here = os.path.dirname(os.path.abspath(self.path))
        session = 1
        cur_file = None
        entries = []            # [no, session, audio, file, index0, index1]
        with open(self.path, "r", encoding="latin1") as fh:
            for line in fh:
                w = line.strip()
                m = re.match(r'REM SESSION (\d+)', w)
                if m:
                    session = int(m.group(1))
                    continue
                m = re.match(r'FILE "(.*)" BINARY', w)
                if m:
                    cur_file = os.path.join(here, m.group(1))
                    continue
                m = re.match(r'TRACK (\d+) (\S+)', w)
                if m:
                    entries.append([int(m.group(1)), session,
                                    m.group(2) == "AUDIO", cur_file, None, None])
                    continue
                m = re.match(r'INDEX (\d+) (\d+):(\d+):(\d+)', w)
                if m and entries:
                    at = msf(int(m.group(2)), int(m.group(3)),
                             int(m.group(4))) * SECTOR
                    entries[-1][4 if m.group(1) == "00" else 5] = at

        # One stream per session, so a lead-in the read offset has pushed
        # across a file boundary is still found.
        streams = {}
        for s in sorted({e[1] for e in entries}):
            files = []
            for e in entries:
                if e[1] == s and e[3] not in files:
                    files.append(e[3])
            streams[s] = _Files(files)

        self.tracks = []
        lba = 0
        for e in entries:
            no, sess, _, path, i0, i1 = e
            st = streams[sess]
            base = next(b for b, _, p in st.parts if p == path)
            fsize = os.path.getsize(path)
            pregap = i1 - (i0 if i0 is not None else i1)
            # lba counts from the start of the track's file, pregap included,
            # which is the convention a .jcd's TOC keeps.
            t = Track(no, sess, True, lba, (fsize - (i1 or 0)) // SECTOR)
            lba += fsize // SECTOR
            start = base + (i1 or 0)
            end = base + fsize
            # Every track of a Jaguar CD is marked AUDIO; a data track is one
            # whose byte-swapped header turns up near its start.
            hdr = swap16(HEADER + b"\0")[:len(HEADER) - 1]
            probe_lo = max(0, base - SECTOR)
            window = st.read(probe_lo, (start - probe_lo) + 32 * SECTOR)
            k = window.find(hdr)
            if sess > 1 and k >= 0:
                pos = probe_lo + k - LEADIN_LEN
                t.audio = False
                t.size = end - pos
                t._read = self._cue_reader(st, pos)
            else:
                t.size = end - start
                t._read = self._cue_reader(st, start)
            self.tracks.append(t)

    @staticmethod
    def _cue_reader(stream, base):
        def read(pos, n):
            lo = pos & ~1
            raw = stream.read(base + lo, ((pos + n + 1) & ~1) - lo)
            return swap16(raw)[pos - lo:pos - lo + n]
        return read


# ---------------------------------------------------------------------------
# the command line
# ---------------------------------------------------------------------------

def _printable(tag):
    return tag and all(32 <= c < 127 for c in tag)


def _tagname(t):
    if t.audio:
        return "audio"
    if t.boot:
        return "boot"
    if _printable(t.tag):
        # a tag is four bytes of anything: keep what a filename can hold
        name = re.sub(r"[^A-Za-z0-9_-]", "_", t.tag.decode("latin1").strip())
        return name or "data"
    return "data"


def cmd_info(d):
    print("%s  (%s, %d tracks)" % (d.path, d.kind, len(d.tracks)))
    print()
    print("%3s %4s %6s %8s %9s %11s %11s %4s %8s  %s" %
          ("tr", "sess", "kind", "lba", "blocks", "image bytes", "tailer at",
           "idx", "tag", "notes"))
    for t in d.tracks:
        kind = "audio" if t.audio else "data"
        idx = "-" if t.index is None else str(t.index)
        tag = "-"
        if t.tag:
            tag = t.tag.decode("latin1") if _printable(t.tag) else t.tag.hex()
        note = ""
        if t.boot:
            note = "boot: %d bytes at $%06X" % (t.boot[1], t.boot[0])
        elif not t.audio and t.tag is None:
            first = t.read(t.payload, 8)
            note = "opens %s" % first.hex()
        tail = "-" if t.tailer is None else str(t.tailer)
        print("%3d %4d %6s %8d %9d %11d %11s %4s %8s  %s" %
              (t.no, t.session, kind, t.lba, t.blocks, t.size, tail, idx, tag,
               note))


def cmd_extract(d, out, payload):
    os.makedirs(out, exist_ok=True)
    for t in d.tracks:
        if t.audio:
            path = os.path.join(out, "track%02d_audio.wav" % t.no)
            with open(path, "wb") as fh:
                t.wav(fh)
        else:
            path = os.path.join(out, "track%02d_%s.bin" % (t.no, _tagname(t)))
            start, stop = (t.data, t.end) if payload else (0, t.size)
            with open(path, "wb") as fh:
                pos = start
                while pos < stop:
                    chunk = t.read(pos, min(1 << 22, stop - pos))
                    fh.write(chunk)
                    pos += len(chunk)
        print("  %s  (%d bytes)" % (path, os.path.getsize(path)))
    for t in d.tracks:
        if t.boot:
            load, img = t.boot_image()
            path = os.path.join(out, "boot_%06X.bin" % load)
            open(path, "wb").write(img)
            print("  %s  (%d bytes, loads at $%06X)" % (path, len(img), load))


def cmd_hex(d, no, off, length):
    t = d.track(no)
    data = t.read(off, length)
    for i in range(0, len(data), 32):
        row = data[i:i + 32]
        asc = "".join(chr(c) if 32 <= c < 127 else "." for c in row)
        print("%#010x  %s  |%s|" % (off + i, row.hex(" "), asc))


def _audio_shift(a, b):
    """The byte shift that lines two audio tracks up, searched over +-16.

    Taken from the middle of the track, where there is music rather than the
    silence every shift would match."""
    p = (min(a.size, b.size) // 2) & ~3
    x = a.read(p, SECTOR * 4)
    y = b.read(p - 16, SECTOR * 4 + 32)
    if len(set(x)) < 16:
        return None
    for s in range(0, 33, 2):
        if y[s:s + len(x)] == x:
            return s - 16
    return None


def cmd_same(a, b):
    """Every track of two images of the same disc, byte for byte.

    A data track is compared from its lead-in to the end of the 'ATRI's after
    its tailer.  A .jcd stops there and a .bin runs on to the end of the
    track, so what lies beyond - zero, or on a few tracks some bytes nothing
    addresses - is reported and not compared.  An audio track is compared
    whole, as its WAV comes out."""
    bad = 0
    for ta in a.tracks:
        try:
            tb = b.track(ta.no)
        except KeyError:
            print("track %2d: only in %s" % (ta.no, a.path))
            bad += 1
            continue
        if ta.audio != tb.audio:
            print("track %2d: audio in one, data in the other" % ta.no)
            bad += 1
            continue
        # a.read(p) is b.read(p + shift).  Zero for data, which both images
        # anchor on the lead-in; for audio, what wav() corrects, and the
        # comparison is of what the WAVs come out as.
        shift = 0
        if ta.audio:
            shift = ta._wav_shift - tb._wav_shift
            found = _audio_shift(ta, tb)
            if found != shift:
                print("track %2d: audio lines up at a shift of %s, "
                      "wav() assumes %d" % (ta.no, found, shift))
                bad += 1
                continue
        oa, ob = max(-shift, 0), max(shift, 0)
        n = min(ta.closed - oa, tb.closed - ob)
        pos = 0
        diff = None
        step = 1 << 22
        while pos < n:
            k = min(step, n - pos)
            xa = ta.read(oa + pos, k)
            xb = tb.read(ob + pos, k)
            if xa != xb:
                diff = pos + next((i for i in range(min(len(xa), len(xb)))
                                   if xa[i] != xb[i]), min(len(xa), len(xb)))
                break
            pos += k
        nonzero = 0
        for t, used in ((ta, oa + n), (tb, ob + n)):
            if t.closed - used > abs(shift):   # one image stops short of it
                nonzero += 1
        # Past the tailer: reported, not compared.  Nothing addresses it.
        beyond = []
        for img, t in ((a, ta), (b, tb)):
            if not t.audio and t.tailer is not None:
                extra = t.last_nonzero() - t.closed
                if extra > 0:
                    beyond.append("%d bytes past the tailer only in the %s"
                                  % (extra, img.kind))
        if diff is not None:
            print("track %2d: DIFFER at byte %d" % (ta.no, diff))
            bad += 1
        elif nonzero:
            print("track %2d: equal over %d bytes, but the longer image has "
                  "data past that" % (ta.no, n))
            bad += 1
        else:
            what = "audio" if ta.audio else "data to the tailer"
            print("track %2d: %s equal, %d bytes%s%s" %
                  (ta.no, what, n,
                   (", at a shift of %d" % shift) if shift else "",
                   ("; " + ", ".join(beyond)) if beyond else ""))
    print("%d tracks, %d differ" % (len(a.tracks), bad))
    return 1 if bad else 0


def main(argv=None):
    ap = argparse.ArgumentParser(prog="python -m jaguarkit.disc",
                                 description="Jaguar CD images: .jcd, .cue/.bin")
    ap.add_argument("image")
    ap.add_argument("--extract", metavar="DIR")
    ap.add_argument("--payload", action="store_true",
                    help="with --extract: data tracks from past the header "
                         "and sync pad to the tailer, rather than whole from "
                         "block 0")
    ap.add_argument("--boot", metavar="FILE", help="write the boot image")
    ap.add_argument("--hex", nargs=3, metavar=("TRACK", "OFF", "LEN"))
    ap.add_argument("--same-as", metavar="IMAGE")
    a = ap.parse_args(argv)

    d = Disc(a.image)
    if a.same_as:
        return cmd_same(d, Disc(a.same_as))
    if a.extract:
        cmd_extract(d, a.extract, a.payload)
    elif a.boot:
        t = next((t for t in d.tracks if t.boot), None)
        if not t:
            sys.exit("no boot track")
        load, img = t.boot_image()
        open(a.boot, "wb").write(img)
        print("%s: %d bytes, loads at $%06X" % (a.boot, len(img), load))
    elif a.hex:
        cmd_hex(d, int(a.hex[0]), int(a.hex[1], 0), int(a.hex[2], 0))
    else:
        cmd_info(d)
    return 0


if __name__ == "__main__":
    sys.exit(main())
