# 07 — Scene track and pixel format

Status: **solved.** The container, the slot layout, the camera footer, the pixel
format and the per-pixel obfuscation are all settled and verified against the
disc. All 672 backdrops and their Z-buffers extract cleanly, and phase 3 added
what the two halves *mean*: the matrix arrangement (7.5) and the depth
encoding (7.6).

Reference implementation: [tools/scene/scenex.py](../tools/scene/scenex.py).

---

## 7.1 The 16-bit pixel format

The Jaguar's `RGB16` mode is **not** the usual RGB565. The bit layout is:

```
bit  15 14 13 12 11 | 10  9  8  7  6 |  5  4  3  2  1  0
      R  R  R  R  R |  B  B  B  B  B |  G  G  G  G  G  G
```

Five bits of red, then five of **blue**, then six of green.

```c
r = ((px >> 11) & 0x1F) * 255 / 31;
b = ((px >>  6) & 0x1F) * 255 / 31;
g = ( px        & 0x3F) * 255 / 63;
```

This was first pinned down from the `SKELSKIN.EXE` model files in the July
source, which carry the original 3D Studio RGB triple in a comment beside each
colour constant:

| Constant | Value | Decoded R5 B5 G6 | Comment in the source |
|---|---|---|---|
| `COLOURmerlot790` | `$7BDF` | 123, 125, 123 | `r = 127, g = 127, b = 127` |
| `COLOURmerlot791` | `$9A5E` | 156, 121, 74 | `r = 155, g = 122, b = 74` |
| `COLOURmerlot796` | `$58D0` | 90, 64, 24 | `r = 88, g = 67, b = 29` |
| `COLOURmerlot797` | `$6913` | 106, 76, 32 | `r = 107, g = 79, b = 35` |

It is now **confirmed on the shipped backdrops as well**: decoding a decoded
scene as R5 B5 G6 gives green grass, blue sky and brown rock; the same bits read
as RGB565 give a uniformly purple image.

## 7.2 Scene slot layout

Track 4 (`PICT`), payload at file offset 31,236,768.

```
scene stride    258,720 bytes = 110 blocks exactly   (matches BO_SCENE_* step $6e)
  data          256,000 bytes   64,000 colour pixels then 64,000 depth values
  footer             48 bytes   camera
  slack           2,608 bytes   zero fill
  next tag           64 bytes   "PICT" repeated 16 times
scene count     672
```

The `PICT` tag block sits at the **end** of each slot, introducing the next
one; scene 0 needs none because the track's own 64-byte content tag serves that
purpose. Scene *n* therefore starts at exactly `n * 258,720`.

The GPU relies on that tag: the scene module scans the CD buffer for the long
`'PICT'`, then for sixteen of them in a row, and takes the byte after the
sixteenth as the start of the payload.

672 scenes against the 594 in `CDLINK.INC` — 78 camera views were added between
July and October 1995.

## 7.3 The obfuscation: XOR with an 8 KB key

The payload is **not compressed**. It is XORed with an 8,192-byte key held in
the resident binary at address **`$30610`**.

The game does this on the GPU. The scene module (see
[08-code-and-gpu.md](08-code-and-gpu.md)) programs the blitter like this:

```
A1_BASE  = scene payload (phrase aligned)   destination
A1_FLAGS = $0028                            32 bits per pixel
A1_PIXEL = 0
A2_BASE  = $30610                           source: the key
A2_FLAGS = $8028                            32 bpp, masked
A2_MASK  = $000007FF                        wrap the source every 2,048 pixels
A2_PIXEL = 0
B_COUNT  = $00017D00                        1 row of 32,000 pixels
B_CMD    = $00C00009                        SRCEN | DSTEN | LFU (~A&B)|(A&~B)
```

`B_CMD` bit 0 is SRCEN and bit 3 DSTEN; bits 22 and 23 select the logic function
`(~A & B) | (A & ~B)`, which is exactly **XOR**. So the blit rewrites the scene
in place as `payload ^= key`.

Two details decide the key:

* **The key is 8,192 bytes, not 2,048.** `A2_MASK` masks the *pixel* index, and
  the blit runs at 32 bits per pixel, so `$7FF` wraps after 2,048 pixels =
  8,192 bytes. Measured: XORing scene 0 with a 2,048-byte key drops the entropy
  from 7.81 to 7.58 bits/byte — noise; with the 8,192-byte key it drops to 5.37
  and the image appears.
* **The key index restarts at the halfway point.** The module issues the blit
  twice, 32,000 pixels each time, and between the two it writes `A2_PIXEL = 0`
  again while leaving A1 to carry on. So byte *i* of each 128,000-byte half is
  XORed with `key[i mod 8192]`, not `key[(half*128000 + i) mod 8192]`. Getting
  this wrong decodes the colour half correctly and leaves the depth half as
  noise, because 128,000 is not a multiple of 8,192.

The key itself is a **patched copy of the first 8 KB of the game code**. The
region `$30610` onward mirrors `$5000` onward instruction for instruction for a
few hundred bytes and then diverges. It looks like code, which is presumably the
point — it does not read as a key sitting in the binary, and it is resident for
free. Take it from `$30610`; the copy at `$5000` is not identical and will not
decode.

## 7.4 Decoding, end to end

```python
key = boot[code_off + 0x30610 - 0x5000:][:8192]     # code_off = end of "CODE"x16
pad = numpy.resize(key, 128000)
data = bytearray(slot[:256000])
data[:128000] ^= pad
data[128000:] ^= pad
colour = data[:128000]      # 320x200, R5 B5 G6
depth  = data[128000:]      # 320x200, 16-bit Z
```

Verified over all 672 scenes: the depth half becomes a smooth surface (mean
absolute horizontal difference typically a few hundred out of 65,535) and the
colour half a coherent picture. The eight scenes that measure roughest are
genuinely busy images — heavily dithered canyon rock and foliage — not
mis-decodes.

## 7.5 The camera footer

48 bytes at offset 256,000, then zero fill:

```
+0   .w   scene id          unique per scene, 64 .. 3084, always increasing
+2   9.w  rotation matrix   3x3, s1.14
+20  3.l  translation       x, y, z
+32  1.l  672               the same value in every scene
+36  2.l  zero
+44  .w   varies            per scene: 234, 197, 300, 144, 100, 259, ...
+46  .w   zero
```

The matrix is a genuine rotation. Scene 671 carries `$4000` — exactly 1.0 in
s1.14 — alongside `$2D94` (11668) and `$D313` (-11501):

```
11668^2 + 11501^2 = 268,415,225      sqrt = 16383.99 ~ 16384
```

A perfect unit vector. This also independently confirms that the 32-bit byte
reversal is correct for this track's payload, not just for its header. The
layout matches the `VIEW MATRIX ARRANGEMENT IN MEMORY` note in `3DENGINE.GAS`.

**The nine words are the rows**, and the world they map from has **y up**:
`m[1]` — the y component of the first row, the camera's own right vector — is
zero in all 672 scenes, which is a camera with no roll read one way and nothing
at all read the other. The transform is `v = M · (w − T)`, `T` being the
translation at +20, that is the camera's world position; view z comes out
negative in front of the camera. Projecting a set's collision mesh through it
lands the walkable area on the floor of the picture. See
[13-viewer.md](13-viewer.md) 13.1.

The scene id at +0 is unique across all 672 slots and strictly increasing, but
with gaps — it is a global identifier, not the slot index. Session 4 mapped the
ids to sets: the same 16-bit value appears in each set's scene table, and it is
structured as `group * 64 + camera` ([10-set-track.md](10-set-track.md) §10.2),
which is what lets every backdrop be named `<SET>_CAM<nn>`.

## 7.6 The depth half: what the numbers mean

The stored 16-bit value is **`65536 − |z|`** — the view-space z, negative in
front of the camera, written as a two's-complement word. Nearer is larger, and
the distance in front of the camera is `65536 − depth`.

It is measured rather than assumed: projecting every set's collision-mesh
vertices, whose distance from the camera follows from geometry alone, into every
scene that set owns gives 35,603 (known `|z|`, stored depth) pairs, and the ratio
`(65536 − depth) / |z|` peaks in the bin 0.99 to 1.00. Occluders can only pull
an individual reading nearer, never further, which is why the median sits lower
at 0.80 while the peak is on 1.

Full argument, cross-checks and the one thing that does not reconcile — the
blitter's `ZMODELT` sense — in [13-viewer.md](13-viewer.md) 13.3.

## 7.7 Appendix: what the empirical search ruled out

Kept because it cost a session and because it is a useful negative result: none
of this was ever going to work, since the data was obfuscated rather than
formatted unusually.

| Hypothesis | Result |
|---|---|
| Plain 320x200 raster, either half | noise |
| per-pixel and per-phrase interleave | noise |
| byte-order variants: none / 16-bit swap / 32-bit reversal / word swap | all noise |
| widths 160, 200, 240, 256, 320, 384, 400, 512, 640 | no width wins |
| tiled layouts, 17 tile widths from 2 to 128 | flat at every scale |
| delta coding (global cumsum, per-row cumsum) | worse than raw |
| bit-planar (16 planes of 8,000 bytes) | noise |
| "take n words every m" sweep, m = 1..16, n = 1..m | nothing above baseline |

What the bit-level analysis *did* show — that bit 15 correlated spatially at
0.80-0.84 while bits 14 and 13 looked random, and that half A was mostly
bit15 = 0 while half B was mostly bit15 = 1 — was real signal leaking through
the XOR, not structure in the encoding. A repeating key leaves exactly that kind
of residue: the plaintext's own statistics survive in whichever bit positions
the key happens to be biased in.

**The lesson worth keeping: entropy 7.8 bits/byte with a strongly non-uniform
byte histogram is the signature of a repeating XOR, not of a codec.** That
combination was in the session-2 measurements and should have pointed here a
session earlier.
