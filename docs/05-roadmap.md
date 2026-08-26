# 05 — Porting strategy and roadmap

## 5.1 Decisions taken

| Decision | Choice |
|---|---|
| Language | **C99** |
| Platform | **SDL3** (video, audio, input), built with a plain **Makefile** |
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

### Phase 2 — Get the assets out — DONE
Extractors for scenes (backdrop plus Z), cinepaks, models, animations, sets,
character sheets and sounds. Output into `assets/` with a JSON manifest named
from `CDLINK.INC` / `DATA.INC` / `WORLD.INC`.

**Both success criteria are met.** All 672 backdrops and Z-buffers extract as
PNG ([07-scene-format.md](07-scene-format.md)), and the wine bottle extracted
from the disc is facet-for-facet identical to `MERLOT79.INC` (§3.3). Also done:
the **models** (239 of them, as OBJ), the **localised text** in three languages,
the **film inventory**, and the identity and record format of every data track
but one ([06-jcd-format.md](06-jcd-format.md)).

Since then: the **animations** (327 of them), the **set track** with its scene
tables, doorways, collision meshes and event lists
([10-set-track.md](10-set-track.md)), and the **script VM**, whose bytecode
carries the puzzle and cutscene logic and places 32 of the 36 films
([11-script-vm.md](11-script-vm.md)).

And the **audio**: the `WAVE` sound-effect bundles on tracks 5 and 6 (78 of
them, 98.7 seconds) and the **dialogue**, which is interleaved with the video
inside the films — 18 minutes 23 seconds of it, extracted through the `STAB`
sample table ([09-text-and-fmv.md](09-text-and-fmv.md) §9.3).

And the **world state**: the 197 objects and characters of the game, with the
40 character sheets they wear, both static tables in the resident binary
([12-world-and-sheets.md](12-world-and-sheets.md)).

And the **manifest**, this phase's stated deliverable:
[tools/manifest.py](../tools/manifest.py) writes one JSON tying the 672 scenes,
48 sets, 27 scripts, 197 world records, 40 character sheets, 239 models, 327
animations, 36 films, 78 waves and 60 localised text records together, each
cross-referenced by name and index. It calls the extractors rather than
re-implementing them, so it cannot drift from them.

Track 9 is still unidentified — 156 KB out of 456 MB, and nothing else on the
disc depends on it.

**Success criteria:** backdrops open as PNG; the wine-bottle model extracted
from the CD matches `MERLOT79.INC` in the source.

### Phase 3 — Something on screen — **done**
`src/` builds `hlview`: an SDL3 window over a 320x200 RGB16 framebuffer that
shows any of the 672 backdrops by name, its Z half as grey, an extracted model
on a turntable, and that model composited into a scene and depth-tested against
the backdrop's own Z-buffer.
**Success criterion met:** a wine bottle standing on a tent floor keeps all 74
of its pixels in the open, 33 of 74 with the tent pole across it, and none at
all behind the pole — the same code and scene, one coordinate moved.

Three conventions could not be settled by reading and are now settled by
drawing ([13-viewer.md](13-viewer.md)):

* **The camera matrix is row major over a y-up world** — `m[1]` is zero in all
  672 scenes, which only means something read as rows, and projecting a set's
  collision mesh through it lands the walkable area on the floor of the picture.
* **The Z-buffer holds `65536 − |z|`**, the negative view z as a two's-complement
  word, so nearer is larger. Measured over 35,603 mesh points across the 48 sets.
* **Item models are stored on their side and the world records stand them up**,
  with `face` elevation 192 — −90° on the game's 256-step circle. Characters are
  y-long already and carry azimuth only.

Four facts carried in from phase 2 held up unchanged: pixels are R5 B5 G6, the
camera is the scene's own 48-byte footer, models are z up for the items, and the
projection is `3DENGINE.GAS`'s

```
sx = 159 + (x * 300) / max(|z|, 40)
sy =  99 + (y * 246) / max(|z|, 40)      ; 246 = 300 * ASPECT, ASPECT = $347A / 2^14
row = 199 - sy
```

One thing the viewer turned up and could not close at the time: an object placed
at the collision mesh's height sinks into the ground the backdrop draws. Phase 4
opened by answering it — the mesh is a flat plane through the bottom of ground
the art models with relief, and the height word is the floor's world y at 1:1
(13.6).

### Phase 4 — The world exists — **done**
Port the data structures (WST, ACT, CIT, DDA, character sheets), set loading,
the collision mesh and triangle search (`FINDTRI`), and character movement with
ground height and stair handling (`SMOOTH.TXT`). Fixed cameras that switch when
event lines are crossed.
**Success criterion:** you can walk around `DUN1` and the camera cuts where it
should.

Done so far:

* **Set loading.** `src/game/set.c` reads a set off track 3 — views, doorways,
  events and the floor mesh — and agrees with `setx.py` set for set. The viewer
  draws the mesh over the backdrop with `--mesh`, which is the debugging view
  the rest of this phase will be built against.
* **The triangle search.** `FINDTRI.GAS` is a search over a list rather than a
  walk down one path, and the difference is not academic: greedy fails on 258 of
  the 5,342 triangles on the disc, the list search on none.
  `hlview --check-mesh` is the regression test — 5,342 searches from a connected
  start, 0 wrong, 0 falling back to a full scan, 5.1 triangles examined on
  average ([13-viewer.md](13-viewer.md) 13.8).
* **Ground height, settled.** The collision triangle's height word is the world
  y of the floor at 1:1, measured against what the backdrops actually draw
  (13.6). The question phase 3 left open — objects sinking into the ground — is
  answered: it is relief in the art that the flat mesh approximates, not a
  format we were reading wrong.
* **The player, driven, and the doorways.** `src/game/control.c` is
  `AICTRL.GAS`'s `PlayerControl` and `ActionCode` — the pad picks the animation
  and the animation's own root motion does the walking — and a `SCENE` event
  that names another set's view loads that set and stands the character on its
  floor, from the init table keyed on the view being left
  ([14-characters.md](14-characters.md) 14.8, 14.9).
* **A second character.** `src/game/sheet.c` reads the world table and the
  character sheets out of the resident binary, and `cshBehaviour` — one byte of
  one sheet — says what a character does when nobody drives him.
  `src/game/ai.c` is the other half of `AICTRL.GAS`: the fourteen AI commands,
  and the arctan bearing behind all of them. Ramirez follows the player and
  arrives through the other half of every doorway
  ([14-characters.md](14-characters.md) 14.10).

**The success criterion is met**: `hlview --scene DUN1_CAM00 --char 0 --drive`
walks Quentin around `DUN1`, the camera cuts where the event lines say, the
doors lead into the next set — and Ramirez comes too.

### Phase 5 — The game moves
Animation with tweening, character-to-character collision, combat (`PCOL.TXT`),
AI (`AICTRL.GAS`), item pickup and inventory.

Started: the AI's movement half is in — `face`, `goto` and `follow` — so what
phase 5 owes it is `AIAttackCode`'s and `AIShootCode`'s other half, which needs
`actStatus`, the hit frames and the opponent's own joypad.
**Success criterion:** you can fight a Hunter and one of you dies.

### Phase 6 — The game tells a story — **started**
The script VM (60+ opcodes), a `.SCT` compiler (to rebuild scripts from the
available source and for debugging), scene events, world state bits, FMV
triggers.
**Success criterion:** the main menu works and the intro plays.

Done so far: **the machine itself.** `src/script/vm.c` is `SCRIPT.GAS`'s
interpreter — all 83 opcodes, the process table, the condition masks, the
world-state and instance access — running on `src/game/act.c`, the active
character table `AICTRL.GAS`'s two master loops walk. `hlview --check-script`
loads every one of the twenty-seven set scripts beside the resident one and
runs them: 585,226 commands, nothing past the dispatch table, no process
overrunning its budget, and every `camera` naming a view its set lists
([11-script-vm.md](11-script-vm.md) 11.9).

And **the loop it runs in**: `src/game/game.c` is one game frame in the order
the original runs it — the machine, the events it posts, the two master loops
over the active characters, the event lines on the floor. `--drive` is that
loop now rather than a branch inside the viewer, and the sewers' sluice moves
the collision mesh under the player because `SE`'s own script says so
([11-script-vm.md](11-script-vm.md) 11.10).

Still to do here: the films, the menu, and the state that survives a `reset`.

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
  util/        file and JSON reading
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
