# Session 4 — The scenes are open

## The headline

**The backdrops decode.** All 672 of them, plus their Z-buffers, extracted as
PNG. The scene payload was never compressed and never oddly formatted: it is
XORed with an 8,192-byte key that sits in the resident binary at `$30610`.

The route there was the one the last session's TODO called for — stop guessing,
read the code — and it took two new tools:

1. **[tools/m68k/dis68k.py](../../tools/m68k/dis68k.py)** — recursive-descent
   68000 disassembler on capstone, with the Jaguar hardware registers and the
   Jaguar CD BIOS jump table named.
2. **[tools/gpu/disgpu.py](../../tools/gpu/disgpu.py)** — a Jaguar RISC
   (GPU/DSP) disassembler.

With those, in order: the boot track loads at `$4000` (its "content tag" is the
load address, `00 00 40 00`); the game proper lives at `$5000`; the 68000 posts
GPU overlays by writing a module-header address into the mailbox at `$F03FFC`;
twenty-one such modules exist; exactly one of them carries the immediates
`$3E800` (256,000) and `$1F400` (128,000). That one, at `$29890`, is the scene
decoder, and it is posted from inside the CD service routine at `$7FE0`.

Reading it: after finding the `PICT` tag it programs the blitter with
`B_CMD = $C00009` — SRCEN, DSTEN and the logic function `(~A&B)|(A&~B)`, which
is XOR — source `A2_BASE = $30610`, `A2_MASK = $7FF`.

Two details had to be right or the image stays noise:

* the mask applies to the **pixel** index and the blit runs at 32 bpp, so the
  key is **8,192** bytes, not 2,048;
* the module blits twice, resetting `A2_PIXEL` in between, so the key index
  **restarts at the halfway point** — and 128,000 is not a multiple of 8,192,
  so a continuous index decodes the colour half and ruins the depth half.

The key is a patched copy of the game's own first 8 KB. It reads as code, which
is presumably why it was chosen.

Details in [07-scene-format.md](../07-scene-format.md) and
[08-code-and-gpu.md](../08-code-and-gpu.md).

## Also settled

* **Track 5 = models, animations and waves** in a per-entity bundle;
  **track 8 = animations**. Both match the July `STRUCDEF.INC` structures byte
  for byte: `FLP - VLP == (vertices + origins) * 8` for every model,
  `size == 14 + framesize * frames` with `framesize = 66 = 20 + 3 * 15` for
  every animation. Fifteen models per character and fifteen animated pieces per
  animation is the same number arrived at from two directions.
* **Track 6 = 38 `WAVE` bundles** on the same 56-block slot stride.
* **The pixel format is confirmed on shipped data.** R5 B5 G6 gives green grass
  and blue sky; RGB565 gives a purple image. Previously this rested only on the
  July model files.
* **The speech is in the films.** Every Cinepak chunk carries a `STAB` sample
  table interleaved with its video. The sampled-audio tracks hold about 98
  seconds in total, which is sound effects, not 88 lines of dialogue.
* **The item text extracts**, English / French / German, indexed by triples of
  pointers scattered through the data area. The accented capitals are stored as
  lowercase letters — `b` = Ä, `d` = Ö, `f` = Ü — and the French carries no
  accents at all.
* **The camera footer is fully laid out**: scene id, 3x3 s1.14 rotation,
  translation, and three fields still unexplained.
* **How the disc is addressed**: `sub_007E9E` walks the BIOS table of contents
  at `$2C00` to the first data track and indexes by data type, so type *n* is CD
  track *n* + 2 — confirmed at the one call site that names a type, the Cinepak
  player asking for type 5.

## The models, later the same session

**The second phase-2 criterion is met.** The wine bottle is on the disc and it
is `MERLOT79`: all 60 facets byte-identical to the source — colour, normal,
vertex count, vertex indices. Seven more source models match the same way
(§3.3). 239 models extracted in total, as OBJ with materials.

Two things had to be understood first:

* The item models are **not** on a data track. July's `BO_MODEL_*` list is all
  characters and set pieces; the items are linked into the resident binary, 19
  of them in a run from `$00E1F8`.
* The facet is 12 bytes plus `noofWords * 4`, so 16 for a triangle or a quad —
  the earlier note in this file claiming a 20-byte July facet against a 16-byte
  shipped one was simply a misreading; both are 16.

And one fact the port needs: the retail exporter **swapped Y and Z**. Fitting
the shipped bottle against the floating-point coordinates in the source gives an
affine map with a maximum residual of 0.72 — integer rounding, nothing else. On
the disc, **Z is up**.

## Still open

* **Track 9.** 55,188 bytes of 7.996-bits/byte payload followed by the long
  `$C00DADE0` repeated 25,187 times. Not the scene key, no repeating key at any
  shift up to 20,000, and its content tag is scrambled too. No code requesting
  type `$27` has turned up yet.
* **Which film is which.** The route is the set event lists on track 3:
  `EVENT_TYPE_CINEPAK` events name a film by block offset, and July's
  `CDLINK.INC` still names the sets.
* **The scene id at footer +0** — unique and increasing across all 672, with
  gaps. Mapping ids to sets would give every backdrop a name.
* **The `SLP` payload.** 140 of track 5's 220 models carry 8 to 88 bytes after
  the facet list, always a multiple of 8. Its meaning is unknown.
* Eleven of the nineteen models linked into the binary are unidentified — the
  eight with surviving source files are named, the rest are not.
* Eighteen of the twenty-one GPU modules are still unidentified.

## TODO for next session

1. **Animations.** The extractor is the obvious companion to `modelx`; the
   record format is already verified (§3.4) and the frame layout is 20 bytes
   plus three angle bytes per piece.
2. **Parse the set event lists** on track 3. It gives the film names, the
   scene-to-set mapping, and the event model the port will need anyway.
3. **Track 9**: find the loader for type `$27`. Widening `dis68k` coverage past
   the current 8% (it stops at jump tables) is probably the way in.
4. Work out what the `SLP` payload on 140 of the models is.
5. Decode the `STAB` sample format so the film audio can be extracted.
6. Name more GPU modules — the 3D engine and the compositor are among them, and
   the port will want to know exactly what they did.
