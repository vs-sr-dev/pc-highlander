# 07 — Scene track and pixel format

Work in progress. What is established, and what is still open.

---

## 7.1 The 16-bit pixel format is settled

The Jaguar's `RGB16` mode is **not** the usual RGB565. The bit layout is:

```
bit  15 14 13 12 11 | 10  9  8  7  6 |  5  4  3  2  1  0
      R  R  R  R  R |  B  B  B  B  B |  G  G  G  G  G  G
```

Five bits of red, then five of **blue**, then six of green.

This was pinned down without guesswork, using the model files that survive in
the July source: `SKELSKIN.EXE` emitted each material as a named constant with
the original 3D Studio RGB triple in the comment, which gives free ground truth.

| Constant | Value | Decoded R5 B5 G6 | Comment in the source |
|---|---|---|---|
| `COLOURmerlot790` | `$7BDF` | 123, 125, 123 | `r = 127, g = 127, b = 127` |
| `COLOURmerlot791` | `$9A5E` | 156, 121, 74 | `r = 155, g = 122, b = 74` |
| `COLOURmerlot796` | `$58D0` | 90, 64, 24 | `r = 88, g = 67, b = 29` |
| `COLOURmerlot797` | `$6913` | 106, 76, 32 | `r = 107, g = 79, b = 35` |

All within rounding error of the 5- and 6-bit quantisation. Conversion for the
port:

```c
r = ((px >> 11) & 0x1F) * 255 / 31;
b = ((px >>  6) & 0x1F) * 255 / 31;
g = ( px        & 0x3F) * 255 / 63;
```

Still open: the game sets `VMODE = $6C7`, which has **VARMOD** on, so some pixels
are CRY rather than RGB16. Which ones, and how the hardware decides, has to be
settled on real backdrop data.

## 7.2 Scene track layout is confirmed exactly

Track 4 (`PICT`), payload at file offset 31,236,768.

Measured by locating every run of `PICT` padding inside the track and taking the
spacing: **671 gaps, all identical**. So:

```
scene stride    258,720 bytes = 110 blocks exactly   (matches BO_SCENE_* step $6e)
  payload       258,656 bytes
  padding            64 bytes of "PICT"
scene count     672
```

672 scenes against the 594 in `CDLINK.INC` — 78 camera views were added between
July and October 1995.

## 7.3 The scenes are compressed — this is the open problem

The obvious reading was: 110 blocks holds a 320x200 16-bit backdrop (128,000
bytes) plus its 320x200 16-bit Z-buffer (128,000 bytes) = 256,000, comfortably
inside 258,656. Tempting, and wrong.

Three measurements rule it out:

1. **Entropy is 7.81 to 7.88 bits per byte**, uniform across the whole payload
   in 32 KB windows, on every scene sampled (0, 1, 5, 100, 400). Raw 16-bit
   photographic data sits far lower, and a raw Z-buffer — large flat regions —
   lower still. Nothing in the payload looks raw.
2. **No row stride exists.** Byte autocorrelation over strides from 16 to 4096
   decays monotonically, with no peak at 640 (320 px at 16 bpp) or anywhere
   else. A raster would show one.
3. **Rendering confirms it.** Decoded as 320x200 RGB16 the image shows faint
   large-scale structure but per-pixel noise, under every byte-order variant
   tried (none / 16-bit swap / 32-bit reversal / word swap) and at every
   plausible header offset.

The payload ends in a short run of zeros, so the compressed stream is slightly
shorter than the slot and the slot is zero-padded. The fixed 110-block slot is
what makes `BO_SCENE_*` a simple multiply, and it is preserved even though the
content no longer fills it.

### Why this is a July-to-October change

The July source contains **no scene decompressor**. `SCENEPR.GAS` is scene
*processing*, not unpacking — and in `MAIN.S` its call is even disabled behind
`.if 0`. `MAIN.S` loads a scene with `BlitZCopy`, a straight blitter copy, which
only makes sense for raw data. And 110 blocks is almost exactly 128,000 + 128,000.

So in July the scenes were raw, and compression was added afterwards. Keeping the
same slot size means it did not buy disc space — it bought **load time**: fewer
blocks to read per camera change, on a double-speed drive.

## 7.4 Next step

Do not guess the codec. **The decompressor is on the disc.** The boot track
(track 2, 68000 code) and the still-unidentified data tracks are the place to
find it. Disassembling the retail boot code is the reliable path, and it also
settles the tracks 3, 5, 8 and 9 question at the same time.
