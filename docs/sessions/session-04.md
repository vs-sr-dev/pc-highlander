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

## The set track, later still

Track 3 gives up the rest of the world model, and it closes a loop with the
scenes. Documented in [10-set-track.md](../10-set-track.md).

* Each set's **scene table** lists its views by a 16-bit id and a CD block
  offset, and that id is **the same value the scene's camera footer carries at
  offset 0**. The two tracks are tied together by it.
* The id is `group * 64 + camera`, 48 groups for 48 sets. The tables reference
  **all 672 scenes with nothing left over**, 792 references in total, because
  113 scenes are doorways listed by both sets that share them.
* The **init table** is the arrival points, addressed by the borrowed ids —
  which is what marks them as the other side of a door.
* The **collision mesh** is 2D: vertices are `(x, z)` long pairs and the
  triangle carries the ground height plus three symmetric neighbour links. The
  triangle list ends where the script begins in all 48 sets.
* The **event record** is 36 bytes, and it parses cleanly: a circle on the floor
  plan with a type, a status word written back in place, and up to three data
  words. 1,203 of the 1,282 events are camera changes.

**The sets have names again.** July's `CDLINK.INC` names 46 sets in alphabetical
order and the retail groups are in the same order, so the two lists align in one
monotone pass — three insertions, one deletion, totals agreeing at 672 against
594, and 23 groups anchored by an exact camera count. The check that settles it:
July's `MENU` is a one-scene set, the alignment puts it at the one-scene group
30, and decoding that scene gives the **main menu** — "START GAME / LANGUAGE /
CREDITS" over a stormy hill.

Every backdrop can now be named `<SET>_CAM<nn>` from its id alone.

## Animations

`animx` completes the asset set: **285 animations on track 5 and 42 on track 8**,
570 seconds in total, all 20 fps with fifteen animated pieces.

It also corrects the reading of track 5 from earlier in the session. It is not
that some slots are models and others animations — a character slot holds its
models *and then* its animations. In slot 0 the last model ends at `$1CAC` and
the first animation begins at `$1CAC`, and across the whole track the 220 model
records and the 285 animation records overlap nowhere. Two independent parsers
tiling the same bytes with no gap and no collision is about as good a check as
this work gets.

## Still open

* **Track 9.** 55,188 bytes of 7.996-bits/byte payload followed by the long
  `$C00DADE0` repeated 25,187 times. Not the scene key, no repeating key at any
  shift up to 20,000, and its content tag is scrambled too. No code requesting
  type `$27` has turned up yet.
* **Which film is which.** The `CINEPAK` event's third data word really is the
  film's CD block offset — the two that occur match films 10 and 2 to the block.
  But there are only three such events on the whole disc: the rest of the film
  triggers are in the set scripts, so this needs the script VM.
* **The 24 bytes after the set header**, reading `4, N, 0, 4, 6N, $10000`.
* **The `SLP` payload.** 140 of track 5's 220 models carry 8 to 88 bytes after
  the facet list, always a multiple of 8. Its meaning is unknown.
* Eleven of the nineteen models linked into the binary are unidentified — the
  eight with surviving source files are named, the rest are not.
* Eighteen of the twenty-one GPU modules are still unidentified.

## TODO for next session

1. **The script VM.** `ScriptOffset` is non-zero in 24 of the 48 sets and it is
   where the film triggers, the dialogue and the puzzle logic live. `SCRIPT.GAS`
   and `OPCODES.INC` in the July source are the specification.
2. **Track 9**: find the loader for type `$27`. Widening `dis68k` coverage past
   the current 8% (it stops at jump tables) is probably the way in.
3. Work out what the `SLP` payload on 140 of the models is.
4. Decode the `STAB` sample format so the film audio can be extracted.
5. Name more GPU modules — the 3D engine and the compositor are among them, and
   the port will want to know exactly what they did.
