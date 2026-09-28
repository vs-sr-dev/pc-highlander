"""jaguarkit.cvid - a Cinepak ('cvid') decoder.

The films in Jaguar CD games are plain Cinepak - the codec the rest of the
world has - inside Atari's container (jaguarkit.film).  A game's own decoder
is hand-written blitter and GPU code, and there is no reason to transcribe it:
this is written from the published format and checked against the discs.

A frame is ten bytes and then strips:

    flags(1) length(3) width(2) height(2) strips(2)
    strip:  id(2) size(2) y0(2) x0(2) height(2) width(2)

Strips stack down the picture, each with its own pair of codebooks and the
vectors that index them:

    $20 / $21   V4 codebook, whole / updated under a flag word
    $22 / $23   V1 codebook, likewise        ($24-$27: the same, grey)
    $30         vectors, one flag bit per block: V1 or V4
    $31         vectors, a bit for "coded at all", then the V1/V4 bit
    $32         vectors, V1 throughout

A codebook entry is four Y and a signed U and V and paints a 2x2 block: V1
doubles one entry over the whole 4x4, V4 puts four in its quadrants.  The
colour is Cinepak's own:  R = Y + 2V,  G = Y - U/2 - V,  B = Y + 2U.

An inter frame leaves the blocks it does not code alone, so the decoder keeps
the last picture; a strip with no codebook of its own continues the one
above it.

A film declares the picture it shows, and that need not be a multiple of
four: Myst's are 92x134, 129x89, 120x81.  The frames are coded whole blocks
and say so - 92x136, 132x92, 120x84 - so the decoder works at the size
rounded up to four and `picture()` crops it back.

Checked: every frame of Highlander's 36 films (13,922) and Battle Morph's
(see the kit's README), with no decoder error, and one Highlander frame
byte for byte against an independent C decoder (runtime/jk_cvid.c).
"""

import struct

MAX_STRIPS = 32


def _clip(v):
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
        flag = mask = 0
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
                e[k * 3 + 0] = _clip(y[k] + 2 * v)
                e[k * 3 + 1] = _clip(y[k] - (u >> 1) - v)
                e[k * 3 + 2] = _clip(y[k] + 2 * u)


def coded(n):
    """The coded size for a declared one: whole 4x4 blocks."""
    return (n + 3) & ~3


class Decoder:
    """Feed it frames in order.  `rgb` is the coded picture, RGB24, and
    `picture()` the declared one."""

    def __init__(self, w, h):
        self.show_w, self.show_h = w, h
        self.w, self.h = coded(w), coded(h)
        self.rgb = bytearray(self.w * self.h * 3)
        self.v1 = [Codebook() for _ in range(MAX_STRIPS)]
        self.v4 = [Codebook() for _ in range(MAX_STRIPS)]
        self.frames = self.keyframes = 0
        self.keyframe = False

    def _blk4(self, x, y, cb, idx):
        w, rgb = self.w, self.rgb
        for q in range(4):
            e = cb.e[idx[q]]
            bx, by = x + (q & 1) * 2, y + (q >> 1) * 2
            for r in range(2):
                o = ((by + r) * w + bx) * 3
                rgb[o:o + 6] = e[r * 6:r * 6 + 6]

    def _blk1(self, x, y, cb, i):
        w, rgb = self.w, self.rgb
        e = cb.e[i]
        for r in range(4):
            o = ((y + r) * w + x) * 3
            h = (r >> 1) * 6
            a = e[h:h + 3]
            b = e[h + 3:h + 6]
            rgb[o:o + 12] = a + a + b + b

    def _vectors(self, d, p, size, cid, s, y0, y1):
        end = p + size
        v1, v4 = self.v1[s], self.v4[s]
        inter = cid & 0x01
        v1only = cid & 0x02
        flag = mask = 0
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
                        continue
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
                    self._blk1(x, y, v1, d[p])
                    p += 1
                else:
                    if p + 4 > end:
                        raise ValueError("vectors ran out")
                    self._blk4(x, y, v4, d[p:p + 4])
                    p += 4

    def _strip(self, d, p, size, s, y0, y1):
        end = p + size
        while p + 4 <= end:
            cid, csize = struct.unpack_from(">HH", d, p)
            if csize < 4 or p + csize > end:
                raise ValueError("chunk $%04X of %d bytes overruns its strip"
                                 % (cid, csize))
            cid >>= 8
            body, blen = p + 4, csize - 4
            if cid in (0x20, 0x21, 0x24, 0x25):
                self.v4[s].read(d, body, blen, cid & 1, cid & 4)
            elif cid in (0x22, 0x23, 0x26, 0x27):
                self.v1[s].read(d, body, blen, cid & 1, cid & 4)
            elif cid in (0x30, 0x31, 0x32):
                self._vectors(d, body, blen, cid, s, y0, y1)
            p += csize

    def frame(self, d, p, size):
        """Decode the frame of `size` bytes at d[p]."""
        if size < 10:
            raise ValueError("frame of %d bytes" % size)
        flags = d[p]
        length = struct.unpack_from(">I", d, p)[0] & 0xFFFFFF
        w, h, nstrips = struct.unpack_from(">HHH", d, p + 4)
        # A container may pad a frame to a multiple of four (Myst does).
        if length > size or size - length > 3:
            raise ValueError("frame says %d bytes, its container %d"
                             % (length, size))
        size = length
        if (w, h) != (self.w, self.h):
            raise ValueError("frame is %dx%d, the film says %dx%d"
                             % (w, h, self.show_w, self.show_h))
        if nstrips > MAX_STRIPS:
            raise ValueError("%d strips" % nstrips)
        q = p + 10
        y = 0
        for s in range(nstrips):
            if q + 12 > p + size:
                raise ValueError("strip %d header past the frame" % s)
            _, ssize = struct.unpack_from(">HH", d, q)
            height = struct.unpack_from(">H", d, q + 8)[0]
            if ssize < 12 or q + ssize > p + size:
                raise ValueError("strip %d of %d bytes overruns the frame"
                                 % (s, ssize))
            if s > 0 and not (flags & 0x01):
                self.v1[s].load(self.v1[s - 1])
                self.v4[s].load(self.v4[s - 1])
            self._strip(d, q + 12, ssize - 12, s, y, min(y + height, self.h))
            y += height
            q += ssize
        self.frames += 1
        self.keyframe = not (flags & 0x01)
        if self.keyframe:
            self.keyframes += 1

    def picture(self):
        """The declared picture, cropped out of the coded one, RGB24."""
        if (self.show_w, self.show_h) == (self.w, self.h):
            return bytes(self.rgb)
        row = self.show_w * 3
        return b"".join(bytes(self.rgb[y * self.w * 3:y * self.w * 3 + row])
                        for y in range(self.show_h))
