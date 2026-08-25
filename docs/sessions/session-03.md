# Session 3 — Phase 2: pixel format, scene slots, disc map

## Done

* **Jaguar 16-bit pixel format settled: R5 B5 G6, not RGB565.** Derived from the
  `SKELSKIN`-generated model files in the July source, which carry the original
  3D Studio RGB triple in a comment beside each colour constant.
* **Scene slot layout measured exactly:** 672 slots of 110 blocks, each holding
  256,000 bytes of fixed-size data plus a 48-byte footer. Measured by locating
  every `PICT` padding run in the track — 671 identical gaps.
* **Camera footer solved.** 48 bytes at offset 256,000, view matrix in s1.14.
  Verified numerically: `11668^2 + 11501^2` gives 16384, a unit vector. This
  also independently confirms the 32-bit byte reversal for track payloads.
* **Corrected a wrong conclusion from session 2.** The scenes are *not*
  compressed. The earlier "99% full" figure was an artefact of scanning back
  from the end of the slot: the footer sits at a fixed offset, so the scan always
  stopped in the same place. Every scene holds exactly 256,000 bytes, no padding.
* **Track 3 identified as the sets**, and the July `STRUCDEF.INC` set layout
  verified against shipped data. The self-consistency check is exact: for set 0
  the collision data ends at `Coll + TriOffset + NumTri * 14 = $1788`, precisely
  the value of `ScriptOffset`.
* **Disc map largely completed.** Tracks 3, 5 and 8 share a uniform slot stride
  of 56 blocks (48, 33 and 10 slots). Track 6 is audio samples. Track 2 is the
  complete resident game binary, 282,080 bytes, containing all UI text in
  English, French and German.
* Tool updated to name each track.

## Repository

Translated to English, renamed doc files, made public, added topics.

## Open — the scene pixel encoding

Half A is colour, half B is depth (half A is predominantly bit15=0, half B
bit15=1; scenes agree far more in half B, as two views of one set sharing depth
structure would). Rendering the bit-15 plane shows genuine picture content.

But the encoding is not a plain raster. Ruled out by measurement: both halves as
raster, per-pixel and per-phrase interleave, four byte-order variants, nine
widths, seventeen tile widths, delta coding, bit-planar, and a full "n words
every m" sweep. Bits 15, 12, 11 correlate spatially; bits 14 and 13 do not. See
[07-scene-format.md](../07-scene-format.md) for the full negative results.

One new data point that sharpens the question: **track 4 is the only data track
with high entropy.** Tracks 3, 5, 6, 8 and 9 measure 0.28 to 5.03 bits/byte —
plainly raw. Track 4 measures 7.81 to 7.88. Whatever transform is applied is
applied to the backdrops alone.

## TODO for next session

1. **Crack the scene encoding.** Empirical search is exhausted; read the code.
   - Disassemble the 68000 boot binary (capstone supports M68K) and find the
     scene load path.
   - Locate GPU module headers inside it: a long holding a GPU RAM address
     (`$F03000` upward) followed by a size.
   - Write a small Jaguar GPU disassembler. The ISA is 16-bit and regular; the
     project will need it repeatedly anyway.
   - Fallback: run the game in an emulator and dump `Scenea` / `ZBuffa` after a
     camera change, to get decoded ground truth to work backwards from.
2. **Identify tracks 5, 8 and 9.** Slot counts are 33, 10 and (track 9) a single
   67-block region. Candidates from the July type list: models, animations,
   character sheets, logics, bitmaps, compiled code.
3. **Extract the item description text** from the boot binary — English, French
   and German are all present, and the port needs them.
4. **Map the 36 Cinepak films to names.** The 20 names in `DATA.INC` are in
   alphabetical order, which constrains but does not determine the mapping.
5. **Locate the speech.** The retail disc has no Red Book speech track.
6. Once scenes decode: write the extractors and hit the phase 2 success criteria
   (backdrops as PNG; the wine-bottle model from the CD matching `MERLOT79.INC`).
