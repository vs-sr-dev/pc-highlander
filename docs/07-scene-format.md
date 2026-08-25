# 07 — Scene track and pixel format

Status: **partially solved.** The container, the slot layout and the camera
footer are settled. The pixel encoding is not. This document records what has
been proven and, just as importantly, what has been ruled out — so the next
attempt does not repeat the same experiments.

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

All within rounding error of the 5- and 6-bit quantisation. Conversion:

```c
r = ((px >> 11) & 0x1F) * 255 / 31;
b = ((px >>  6) & 0x1F) * 255 / 31;
g = ( px        & 0x3F) * 255 / 63;
```

## 7.2 Scene slot layout is settled, exactly

Track 4 (`PICT`), payload at file offset 31,236,768.

Measured by locating every run of `PICT` padding inside the track and taking the
spacing: **671 gaps, all identical**.

```
scene stride    258,720 bytes = 110 blocks exactly   (matches BO_SCENE_* step $6e)
  data          256,000 bytes   fixed, no padding, identical for every scene
  footer             48 bytes   camera data
  slack           2,672 bytes   zero fill
scene count     672
```

672 scenes against the 594 in `CDLINK.INC` — 78 camera views were added between
July and October 1995.

The 256,000-byte figure is **exact and constant** across every scene sampled
(0, 1, 2, 3, 10, 50, 100, 300, 500, 671), with zero trailing padding. So the
scene payload is a **fixed-size structure**, not a variable-length compressed
stream. 256,000 = 320 x 200 x 4 bytes, i.e. one 16-bit colour plus one 16-bit
depth value per pixel.

## 7.3 The camera footer

48 bytes at offset 256,000, then zero fill. It holds the view matrix in
**s1.14 fixed point**, matching the `VIEW MATRIX ARRANGEMENT IN MEMORY` note in
`3DENGINE.GAS`.

Confirmed numerically on scene 671, whose footer contains `0x4000` — exactly 1.0
in s1.14 — alongside `0x2D94` (11668) and `0xD313` (-11501):

```
11668^2 + 11501^2 = 268,415,225      sqrt = 16383.99 ~ 16384
```

A perfect unit vector. This is a rotation matrix, and it also **independently
confirms that the 32-bit byte reversal is correct for this track's payload**,
not just for its header.

## 7.4 The pixel encoding — what is ruled out

Everything below was tested and produced noise. Recording it so nobody repeats it.

| Hypothesis | Result |
|---|---|
| Plain 320x200 raster, first half = image | noise |
| second half = image | noise |
| per-pixel interleave `[colour][depth]...` | noise |
| per-phrase interleave (8-byte) | noise |
| byte-order variants: none / 16-bit swap / 32-bit reversal / word swap | all noise |
| widths 160, 200, 240, 256, 320, 384, 400, 512, 640 | no width wins |
| tiled layouts — widths 2, 3, 4, 5, 6, 8, 10, 12, 16, 20, 24, 32, 40, 48, 64, 80, 128 | flat at every scale |
| delta coding (global cumsum, per-row cumsum) | worse than raw |
| bit-planar (16 planes of 8,000 bytes) | noise |
| "take n words every m" sweep, m = 1..16, n = 1..m | nothing above baseline |

Supporting measurements:

* **Byte autocorrelation** over strides 16 to 4096 decays monotonically. No peak
  at 640 (320 px at 16 bpp) or anywhere else, on the full byte and on the high
  byte alone.
* **Adjacent-sample difference** is flat at lag 1 through 12. Even immediate
  neighbours are numerically uncorrelated.
* **Byte histogram is not uniform** — counts range from 455 to 5,591 against a
  mean of 1,000, and every one of the 256 values occurs. Well-compressed or
  encrypted data would be flat, so this is not a general-purpose codec output.
* **Entropy is 7.81 to 7.88 bits/byte**, uniform in 32 KB windows.

## 7.5 The pixel encoding — what is proven

Bit-level analysis is where the structure finally shows up. Agreement of each
bit position between horizontally and vertically adjacent 16-bit samples
(0.50 = random):

| bit | half A horiz | half A vert | half B horiz | half B vert |
|---:|---:|---:|---:|---:|
| 15 | **0.825** | **0.795** | **0.839** | **0.812** |
| 14 | 0.581 | 0.533 | 0.590 | 0.535 |
| 13 | 0.486 | 0.511 | 0.484 | 0.511 |
| 12 | 0.653 | 0.637 | **0.712** | **0.711** |
| 11 | 0.617 | 0.593 | **0.692** | 0.673 |
| 10 | 0.597 | 0.586 | 0.598 | 0.585 |
| 8 | 0.585 | 0.580 | 0.592 | 0.591 |
| 1 | 0.631 | 0.617 | 0.596 | 0.544 |
| 0 | 0.619 | 0.589 | 0.613 | 0.574 |
| others | ~0.50 | ~0.50 | ~0.50 | ~0.50 |

Rendering the bit-15 plane as a 320x200 bitmap shows **genuine picture content**:
in half A a bright horizontal band across the lower third (a lit floor), a
distinct bright blob, and blocky highlights along the top edge. Different scenes
show different content in the same plane. This is not noise.

So the data really is laid out 320 pixels per row, and something image-shaped is
in there. Two further facts constrain it:

* **Half A is predominantly bit15 = 0; half B is predominantly bit15 = 1.**
  Consistent with half A being colour (a mostly dark scene keeps the red MSB
  low) and half B being depth (mostly distant geometry keeps values high).
  Half A mean 21,481, half B mean 49,909, each with about 22,000 distinct
  values out of 64,000 samples.
* **Different scenes agree far more in half B than in half A.** Block-by-block
  agreement between scenes 0 and 1 runs 0.01-0.05 through half A and 0.11-0.13
  through half B, which is what you would expect from two camera views of the
  same set sharing depth structure.

The puzzle is that bits 14 and 13 are near random while bit 15 is strongly
correlated. In a straightforward dithered RGB16 image the whole top of the red
channel would correlate, decaying gradually toward the LSB. It does not. Some
per-pixel transform sits between the stored value and the displayed colour.

## 7.6 Next step: stop guessing, read the code

The empirical search has been taken about as far as it usefully goes. The
authoritative answer is on the disc.

**Track 2 is the complete resident game binary** — 282,080 bytes, ending with
`ATARI APPROVED DATA TAILER ATRI`. It contains all the UI text in English,
French and German (inventory screen, joypad reconfigure, save/load menu, disc
error messages), the string `HIGHLANDER ONE`, and tagged sections such as a
`CODE` block at offset `0x125C0`. It also confirms that the retail build
addresses data by **four-character tags** (`move.l #'CODE',d1`) rather than the
numeric data types of the July source.

Plan:

1. Disassemble the 68000 boot code (capstone supports M68K) and find the scene
   load path.
2. Locate the GPU module headers inside it — in the July source these are a long
   holding a GPU RAM address (`$F03000` upward) followed by a size.
3. Write a small Jaguar GPU disassembler. The ISA is 16-bit and regular, so this
   is a modest amount of work, and the project will need it repeatedly.

A useful fallback if that stalls: run the game in an emulator and dump the scene
buffers from RAM after a camera change. The decoded backdrop and Z-buffer land
in `Scenea`/`ZBuffa`, so a memory dump gives ground truth to work backwards from.
