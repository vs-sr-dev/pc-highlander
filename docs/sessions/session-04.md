# Session 4 — The scenes are open, and most of the assets with them

Phase 2's two success criteria are both met: the backdrops open as PNG, and the
wine-bottle model extracted from the CD matches `MERLOT79.INC`.

Six new tools, four new documents. The through-line is that every question this
session answered was answered by **reading the shipped code, or by cross-checking
two independent readings against each other** — not by guessing at the data.

---

## 1. The backdrops decode

All 672, plus their Z-buffers. The scene payload was never compressed and never
oddly formatted: it is **XORed with an 8,192-byte key** held in the resident
binary at `$30610`.

Getting there needed the shipped code, so it needed two disassemblers:

* **[tools/m68k/dis68k.py](../../tools/m68k/dis68k.py)** — recursive-descent
  68000 on capstone, with the Jaguar hardware registers and the Jaguar CD BIOS
  jump table named.
* **[tools/gpu/disgpu.py](../../tools/gpu/disgpu.py)** — the Jaguar RISC, for
  both GPU and DSP.

With those, in order: the boot track loads at `$4000` — its "content tag" is not
text but the load address, `00 00 40 00`; the game proper lives at `$5000`; the
68000 posts GPU overlays by writing a module-header address into a mailbox at
`$F03FFC`; twenty-one such modules exist; exactly one carries the immediates
`$3E800` (256,000) and `$1F400` (128,000). That one, at `$29890`, is the scene
decoder, and it is posted from inside the CD service routine at `$7FE0`.

Reading it: after finding the `PICT` tag it programs the blitter with
`B_CMD = $C00009` — SRCEN, DSTEN, and the logic function `(~A&B)|(A&~B)`, which
is XOR — source `A2_BASE = $30610`, `A2_MASK = $7FF`.

Two details had to be right or the image stays noise:

* the mask applies to the **pixel** index and the blit runs at 32 bpp, so the
  key is **8,192** bytes, not 2,048;
* the module blits twice, resetting `A2_PIXEL` in between, so the key index
  **restarts at the halfway point** — and 128,000 is not a multiple of 8,192, so
  a continuous index decodes the colour half and ruins the depth half.

The key is a patched copy of the game's own first 8 KB. It reads as code, which
is presumably the point.

→ [07-scene-format.md](../07-scene-format.md), [08-code-and-gpu.md](../08-code-and-gpu.md)

## 2. The models, and the wine bottle

The item models are **not on a data track at all**. July's `BO_MODEL_*` list is
characters and set pieces only; the items are linked into the resident binary,
19 of them in a run from `$00E1F8`. Eight match the surviving source files with
**every facet byte-identical** — colour, normal, vertex count, vertex indices:
`MERLOT79`, `CHEESE`, `LOAF`, `HKEY`, `LOCKET`, `HSWORD_Q`, `HWATAWEE`,
`GASGUN`.

The vertex data did change. Fitting the shipped bottle against the
floating-point coordinates in the source gives an affine map with a **maximum
residual of 0.72** — integer rounding, nothing else. The retail exporter
**swapped Y and Z** and rescaled by about 0.28. On the disc, **Z is up**.

239 models extracted as OBJ. Two format details the shipped data settles: a
facet is 12 bytes plus `noofWords * 4`, so 16 for a triangle or a quad; and
`SLP`, when non-zero, points at the byte right after the facet list, with the
declared length covering that payload too.

→ [03-data-formats.md §3.3](../03-data-formats.md)

## 3. The set track, and the backdrops get names

Each set's scene table lists its views by a 16-bit id plus a CD block offset, and
**that id is the same value the scene's own camera footer carries at offset 0**.
The two tracks are tied together by it. The id is structured as
`group * 64 + camera`, 48 groups for 48 sets, and the tables reference **all 672
scenes with nothing left over** — 792 references, because 113 scenes are doorways
listed by both sets that share them.

Also parsed and verified: the init table (arrival points, addressed by the
borrowed ids, which is what marks them as doorways); the collision mesh (2D,
vertices as `(x, z)` long pairs, triangles carrying ground height and three
symmetric neighbour links, ending where the script begins in all 48 sets); and
the 36-byte event record, of which 1,203 of 1,282 are camera changes.

**The sets have names again.** July's `CDLINK.INC` names 46 sets in alphabetical
order and the retail groups are in the same order, so the lists align in one
monotone pass — three insertions, one deletion, totals agreeing at 672 against
594, 23 groups anchored by an exact camera count. The check that settles it:
July's `MENU` is a one-scene set, the alignment puts it at the one-scene group
30, and decoding that scene gives the **main menu screen**, "START GAME /
LANGUAGE / CREDITS" over a stormy hill. Every backdrop can now be named
`<SET>_CAM<nn>` from its id alone.

→ [10-set-track.md](../10-set-track.md)

## 4. Everything else that landed

* **Track identities.** Track 5 is a **per-entity bundle** — a character slot
  holds its fifteen models and then its animations, with its sounds in the
  neighbouring slot. Track 8 is animations, track 6 is 38 `WAVE` bundles.
* **285 animations on track 5 and 42 on track 8**, 570 seconds, all 20 fps with
  fifteen pieces. The two parsers tile track 5 with no gap and no collision: in
  slot 0 the last model ends at `$1CAC` and the first animation begins at
  `$1CAC`.
* **The pixel format is confirmed on shipped data.** R5 B5 G6 gives green grass
  and blue sky; RGB565 gives a purple image. It had rested only on the July
  model files until now.
* **The item text extracts**, English / French / German, indexed by triples of
  pointers scattered through the data area. Accented capitals are stored as
  lowercase letters — `b` = Ä, `d` = Ö, `f` = Ü — and the French carries no
  accents at all.
* **The speech is in the films.** Every Cinepak chunk carries a `STAB` sample
  table interleaved with its video. The sampled-audio tracks hold about 98
  seconds in total, which is a sound-effect budget, not 88 lines of dialogue.
* **How the disc is addressed.** `sub_007E9E` walks the BIOS table of contents at
  `$2C00` to the first data track and indexes by data type, so type *n* is CD
  track *n* + 2 — confirmed at the one call site that names a type literally.

→ [06-jcd-format.md](../06-jcd-format.md), [09-text-and-fmv.md](../09-text-and-fmv.md)

---

## Still open

* **Track 9.** 55,188 bytes of 7.996-bits/byte payload followed by the long
  `$C00DADE0` repeated 25,187 times, then the data tailer. Not the scene key, no
  repeating key at any shift up to 20,000, and its 64-byte content tag is
  scrambled too. No code requesting type `$27` has turned up.
* **Which film is which.** The `CINEPAK` event's third data word really is the
  film's CD block offset — the two that occur match films 10 and 2 to the block
  — but there are only three such events on the whole disc.
* **The `SLP` payload.** 140 of track 5's 220 models carry 8 to 88 bytes after
  the facet list, always a multiple of 8.
* **The 24 bytes after the set header**, reading `4, N, 0, 4, 6N, $10000`.
* **The camera footer's unexplained fields**: the long reading 672 in every
  scene, and the word at +44 that varies per scene.
* **Eleven of the nineteen** models linked into the binary are unidentified.
* **Eighteen of the twenty-one** GPU modules are unidentified.

---

## TODO for session 5

### 1. The script VM — the biggest remaining unlock

`ScriptOffset` is non-zero in 24 of the 48 sets, and that is where the film
triggers, the dialogue and the puzzle logic live. Everything downstream wants
it: naming the other 34 films, knowing what an event actually *does*, and the
port needing to run this bytecode eventually anyway.

Where to start:

* `SCRIPT.GAS`, `OPCODES.INC`, `SCRIPT.MAC` and the five `SCRIPT*.TXT` notes in
  the July source are the specification; the `.SCT` files (`MENU.SCT`,
  `CHEAT.SCT`, `SAMPLE.SCT`, `DOME1.SCT`) are worked examples to test a
  disassembler against.
* On the disc, set 0's script starts at `$1788`. Sets 10, 11, 29 and 30 have
  tiny ones (`$178`, `$1D0`, `$1A8`, `$158`) — good first specimens.
* Cross-check as you go: any film block offset appearing as an operand should
  match the `filmls` inventory, the way the three `CINEPAK` events did.

### 2. Track 9

Find the loader for data type `$27`. `dis68k`'s recursive pass covers only about
8% of the game binary because it stops at jump tables and computed jumps, so
widening that is probably the way in — concretely, teach the tracer the
`move.w N,d0 / asl #2,d0 / movea.l (table,d0),aN / jmp (aN)` idiom the code uses
at `$50A6` and elsewhere, or triage the `--linear` output for CD calls the
recursive pass never reached.

If the loader stays hidden, the fallback is the emulator: run the game, break on
a read of that track, and see where the data lands and what touches it.

### 3. Film audio

Decode the `STAB` sample table so the film audio can be extracted — that is the
speech, and the port needs it. Entries are 16 bytes (offset, size, timestamp,
type), with types `1` and `$32` observed. `CINEPAK.INC` gives the rates
(`audio_in 22252`, `audio_out 21867`, `audio_size $4000`) and `CINEDSP.DAS` is
the player.

### 4. Extractors still missing

* **waves** — the format is known (`long total; 'WAVE'; long size; 8-bit
  samples`), 38 bundles on track 6 and 36 on track 5. A `wavex` writing `.wav`
  finishes the audio side and is an hour's work.
* **character sheets** — `SHEET.S` and the `CHARSHEET` structure in
  `STRUCDEF.INC`. Not yet located on the disc; they may be linked into the
  binary like the item models were.
* **a manifest** — one JSON tying scene names, set names, models, animations and
  films together. That is what phase 3 will actually consume, and it is the
  stated deliverable of phase 2 in the roadmap.

### 5. Smaller and well-defined

* **Name more GPU modules** from their dispatch sites and their `movei`
  constants. The 3D engine and the compositor are among the eighteen unknown
  ones, and the port wants to know exactly what they did.
* **The `SLP` payload** — 140 specimens, sizes 8 to 88, always a multiple of 8.
* **Identify the eleven unnamed binary models** by rendering them
  (`modelx --png`); several should be recognisable objects.
* **The camera footer's remaining fields** — best found by locating the code
  that reads them: the footer sits at scene payload + 256,000, so look for a
  routine indexing `$3E800` off a scene buffer.

### 6. Then phase 3

A viewer: SDL3, 320x200, a backdrop with its Z-buffer, and an extracted model
composited into it and depth-tested. Everything it needs now exists. Two things
to carry over so they are not rediscovered the hard way: the models are **Z up**,
and the pixels are **R5 B5 G6**.
