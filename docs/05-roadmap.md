# 05 — Porting strategy and roadmap

## 5.1 Decisions taken

| Decision | Choice |
|---|---|
| Language | **C99** |
| Platform | **SDL3** (video, audio, input), built with **CMake** |
| Rendering | **software rasteriser**, 320x200 16-bit framebuffer plus Z-buffer, upscaled through an SDL texture |
| Fidelity | **faithful first, options later** — the original game is the reference; improvements ship as toggles |
| Repository | **strict BYOA** — no 1995 source, no disc image, no assets |

### Why a software rasteriser
The original engine draws **flat-shaded** facets, depth-tested against a
Z-buffer loaded from disc. Reproducing that in software gives us pixel-exact
*ground truth*: if a frame does not match the original running in an emulator,
the bug is ours. The compute cost is irrelevant — a few hundred polygons per
frame over 64,000 pixels. GPU acceleration stays available later, on top of an
engine that is already correct.

## 5.2 What should NOT be ported

A large part of the original source exists purely to work around hardware
limits. Read it to understand *what* it does, do not transcribe it:

* the GPU overlay kernel and the whole blitter choreography (see
  [02-architecture.md](02-architecture.md) §2.2);
* the 448-byte chunk allocator and its garbage collection;
* the state-machine CD queue with priority waits and the data / Red Book mode
  switching;
* the triple scene buffer with prefetch — on PC every asset fits in RAM;
* the CD BIOS and Cinepak library disassemblies (`CDINIT*.GAS`, `CINELIST.GAS`);
* the hand-written Cinepak decoder: FILM container plus a standard codec.

What **must** be reproduced faithfully: integer arithmetic and fixed-point
formats (s15.0 / s1.14 / 8.8), the ordering of operations in the game loop, the
exact semantics of the script VM, the collision and combat algorithms, and the
animation and tweening curves.

## 5.3 Phased roadmap

### Phase 0 — Analysis — DONE
Inventory, architecture, formats and CD addressing documented.

### Phase 1 — Open the disc — DONE
`.jcd` container decoded, data de-swapped, retail layout mapped.
Tool: [tools/jcd/jcdinfo.py](../tools/jcd/jcdinfo.py). Details in
[06-jcd-format.md](06-jcd-format.md).
**Outcome:** the zero point is confirmed to the byte (`BO_CINEPAK_BUZZ = 0`
lands on the first film's `FILM` header), but the July 1995 offset tables do
**not** hold for the rest of the disc and must be rediscovered.

### Phase 2 — Get the assets out — IN PROGRESS
Extractors for scenes (backdrop plus Z), cinepaks, models, animations, sets,
character sheets and sounds. Output into `assets/` with a JSON manifest named
from `CDLINK.INC` / `DATA.INC` / `WORLD.INC`.

Done: the **scenes** — all 672 backdrops and Z-buffers extract as PNG
([07-scene-format.md](07-scene-format.md)); the **localised text** in three
languages; the **film inventory**; the identity and record formats of every data
track but one ([06-jcd-format.md](06-jcd-format.md)).

Open: the model and animation extractors. The record layout is verified but the
shipped facet list is 16 bytes per facet against the 20 of the July source, and
that has to be resolved first. Track 9 is still unidentified.

**Success criteria:** backdrops open as PNG; the wine-bottle model extracted
from the CD matches `MERLOT79.INC` in the source.

### Phase 3 — Something on screen
SDL3 window, 320x200 framebuffer, CRY/RGB16 to RGB888 conversion, and a viewer
that shows a backdrop with its Z-buffer and lets us spin an extracted model
inside it, lit and depth-tested.
**Success criterion:** the model passes correctly behind scenery.

### Phase 4 — The world exists
Port the data structures (WST, ACT, CIT, DDA, character sheets), set loading,
the collision mesh and triangle search (`FINDTRI`), and character movement with
ground height and stair handling (`SMOOTH.TXT`). Fixed cameras that switch when
event lines are crossed.
**Success criterion:** you can walk around `DUN1` and the camera cuts where it
should.

### Phase 5 — The game moves
Animation with tweening, character-to-character collision, combat (`PCOL.TXT`),
AI (`AICTRL.GAS`), item pickup and inventory.
**Success criterion:** you can fight a Hunter and one of you dies.

### Phase 6 — The game tells a story
The script VM (60+ opcodes), a `.SCT` compiler (to rebuild scripts from the
available source and for debugging), scene events, world state bits, FMV
triggers.
**Success criterion:** the main menu works and the intro plays.

### Phase 7 — The game speaks
16-voice PCM mixer, Red Book playback from the image's audio track, synchronised
Cinepak playback, three separate volumes.

### Phase 8 — Polish
Save games, pause menu, HUD and life bar, font, credits screen, NTSC/PAL
handling, modern options behind toggles.

## 5.4 Repository layout

```
docs/          technical documentation
tools/         extractors and utilities, independent of the engine
  jcd/         .jcd container reader
  extract/     per-data-type extractors
src/
  main.c       game loop
  game/        WST / ACT / CIT / sets / scenes / events
  anim/        animation, tweening, collision, combat
  r3d/         flat rasteriser plus Z-buffer
  script/      VM
  media/       Cinepak, Red Book, PCM
  platform/    SDL3
assets/        (git-ignored) extractor output
Highlander/    (git-ignored) the user's own original material
```

## 5.5 Known risks

| Risk | Mitigation |
|---|---|
| ~~The July 1995 offsets do not match the retail disc~~ **confirmed in phase 1** | Rebuild the map by signature scanning. Already done for the 36 Cinepak films; still to do for the other data types |
| ~~Scene / Z-buffer format not documented in the source~~ **still open** | Slot layout and camera footer are solved; the per-pixel encoding is not. Empirical search is exhausted — recover the decoder from the disc's own code (track 2 is the full resident binary) |
| The source is a WIP, not the shipped code | Treat it as a **design specification**, not an oracle. Where it disagrees with the disc, the disc wins |
| The asset conversion tools are missing (Map Tool, SKELSKIN) | Not needed: we read data that is already converted, off the CD. They would only matter for authoring new content |
| CRY vs RGB16 with VARMOD | The RGB16 half is settled (see [07-scene-format.md](07-scene-format.md)); which pixels are CRY still needs real backdrop data |
| Where the speech went | The retail disc has no Red Book speech track. It has to be located among the samples or inside the films |
