# 02 — Engine architecture

## 2.1 The three processors

The Jaguar has a 68000 at 13.3 MHz and two RISC cores at 26.6 MHz (GPU "Tom",
DSP "Jerry"). Highlander uses them like this:

| Processor | Role |
|---|---|
| **68000** | Orchestrator. It does no heavy lifting: it drives the game loop, calls the CD BIOS (the only routines that *must* run on the 68k), shuffles parameters, and restarts the game on game-over. |
| **GPU** | **The entire game.** 3D engine, animation, collision, combat, AI, scene events, script VM, CD controller, text. 23k lines of GPU code. |
| **DSP** | PCM audio mixer (`WAVE.DAS`), joypad reading (`JOYPAD.DAS`), FMV audio (`CINEDSP.DAS`). |

The brutal constraint: the GPU has **4 KB of internal RAM**, shared between
code, stack and variables. So the engine is split into about twenty overlays
that are **blitted into the GPU one at a time** and chained.

## 2.2 The GPU kernel (`KERNEL.GAS`)

The kernel lives permanently at the top of GPU RAM (the last 268 bytes) and is
never overwritten. The bottom of GPU RAM holds six parameter longs
(`PARAM1`..`PARAM6`); the first 400 bytes are reserved for the CD routines.

Protocol:

1. the GPU spins reading `PARAM6`;
2. the 68000 writes into `PARAM6` the main-RAM address of a block shaped
   `[dest_addr.l][size.l][code...]`;
3. the kernel uses the **blitter** to copy that code into the GPU and jumps
   into it;
4. the module, when done, clears `PARAM6` and returns to the kernel.

The 68000 side calls `kernwait`, which waits for `PARAM6` to return to zero.

> For the port this collapses into an ordinary sequence of function calls. The
> whole overlay-and-blitter choreography **should not be reproduced**: it was a
> workaround for the 4 KB limit.

## 2.3 The game loop (`MAIN.S`, label `Gameloop`)

```
Gameloop:
  setvars
  loop:
    input()      ; phase 1
    update()     ; phase 2
    goto loop
```

**`input()`, in order:**

| Step | GPU module | What it does |
|---|---|---|
| 1 | `SCRIPTCODE` | runs one tick of every script VM process |
| 2 | `EVENT` | tests the scene's event triggers against the player position; may fire cinepak / redbook / camera change / character change |
| 3 | (68k) | `cineplayer` if a Cinepak was requested |
| 4 | (68k) | `cdloader`: queues CD requests, handles the data / Red Book mode switch |
| 5 | `EV2` | second event pass for loads that are needed immediately |
| 6 | `NEWSCENE` / `CHARNEWSCN` | on scene change: rebuild the draw list, prefetch neighbouring scenes, possibly change set |
| 7 | (68k) | `readpad`, `movemod`, `movemo2`, `dispfrd` — joypad to movement intent |

**`update()`, in order:**

| Step | GPU module | What it does |
|---|---|---|
| 1 | `ANIMCODE` | advances animations, applies root motion, resolves collision against the collision mesh, computes ground height |
| 2 | `PPCOLL` | character-to-character collision and combat resolution |
| 3 | `ENGINE` | rasterises the 3D characters into the back buffer with Z testing |
| 4 | `BITMAP` | 3D bitmaps (sprites with a Z value) over the scene |
| 5 | `TEXTS` | text and HUD |
| 6 | `COLLECT` | item pickup logic and end-of-frame buffer swap |

## 2.4 Video

* Resolution **320x200, 16 bit** (`VIDSTUFF.INC`).
* `VMODE = $6C7` — RGB16 with the **VARMOD** bit set: the framebuffer is mixed
  **CRY / RGB16** on a per-pixel basis (see `CRYRGB.TXT`, where the authors
  discuss shading quality in both modes explicitly).
* Double buffered (`buff1`/`buff2`, 320 KB each including Z and data) plus three
  scene buffers (`Scenea`/`Sceneb`/`Scenec` with matching `ZBuffa`/`ZBuffb`/`ZBuffc`)
  for caching.
* Both NTSC **and** PAL are supported (`VIDINIT.S`): 60/50 Hz timing, with the
  `framerate` variable used as a multiplier in the animation code, so the logic
  is frame-rate independent by construction.
* Life bar: 40 pixels at the bottom (`BARHEIGHT`).

## 2.5 The 3D engine (`3DENGINE.GAS`, 3,414 lines)

The author left the design documentation in the file header. In summary:

* Right-handed coordinate system, 16-bit integers (s15.0 for positions, s1.14
  for rotation matrices).
* **3x3 matrices plus translation** — no 4x4: the author notes that the eight
  extra ticks per multiply were not worth it.
* Per-polyhedron pipeline: copy the instance matrix, concatenate with the view
  matrix, transform vertices, perspective-project; then per facet: transform the
  normal, **backface cull** (Z <= 0), 2D clip, **flat lighting** (one colour per
  facet), and scan-convert with the **blitter**, writing both the bitmap **and**
  the Z-buffer.
* Lighting: **ambient plus 4 point lights** (RGB, X, Y, Z each, 18 longs total).
* Per-model status bits: bit 0 = invisible, bit 2 = skip lighting (the
  deliberately "cartoony" characters are unlit, which saves a great deal).
* Limits: fewer than 128 vertices and fewer than 255 facets per model, at most
  32 vertices per facet.

`FORMMAT.GAS` builds the instance matrices from three rotations every frame
(order: **Y azimuth, then X elevation, then Z twist**).

`3DBITMAP.GAS` adds 16-bit bitmaps carrying a Z value, merged into the scene.

## 2.6 Compositing against the backdrop

This is the visual heart of the game: the backdrop loaded from the CD carries
its own **Z-buffer, pre-computed from the original 3D Studio scene**. The engine
copies backdrop and Z into the working buffers (`BlitZCopy` in `LIBRARY.S`) and
then draws characters with ordinary depth testing. The result: characters pass
correctly **behind** scenery, at zero geometry cost.

## 2.7 Memory map (2 MB, from `VIDSTUFF.INC`)

Top down:

```
$200000  top
         stack 1K, CD buffer 148K
         buff1  320K  (back buffer 1 + Z + data)
         buff2  320K  (back buffer 2 + Z + data)
         scratch 32K + scratch2/nvram 16K
         buff4/buff5/zbuff  384K  (3 scene buffers + Z)
         vlist/vlist2/origsave/svlist  6K
         snd        128K   audio samples
         modelspc   128K   models
         animspc    196K   animations
         charlgcs    48K   character logic
         bitmaps     32K   3D bitmaps        <- start of the chunked area
         setdata     32K+  set data
         cdq        1.25K  CD queue
         activchar   16K   Active Character Table
         chartbl      8K   Character Instance Table
         draw_data_area      4K
         instance_data_area 16K
         ws / cs     16K + 16K  world state / character sheets
         ...68000 code and resident copies of GPU/DSP code
```

The dynamically managed area uses **448-byte chunks** with one management word
per chunk (top bit = free, remainder = index of the "owner" chunk). Garbage
collection runs on every set change. Documented constraint: character sheet data
must live in the first 700 chunks.

## 2.8 Audio

* **`WAVE.DAS`** — PCM mixer on the DSP, 16 voices (`waveEntries`), with a
  command API (`wavePlay`, `waveStop`, `waveSetVolume`, ...) documented in
  `NOTES.TXT`.
* **Red Book** — speech is genuine CD audio, addressed by block offset on a
  dedicated track (`BO_CDAUDIO_LINE005` through `LINE100`). The `cdloader` has
  to switch between data mode and audio mode, and it is the most delicate part
  of the 68k code. *(Note: this track no longer exists on the retail disc — see
  [06-jcd-format.md](06-jcd-format.md) §6.5.)*
* **Cinepak** — audio at 22,252 Hz resampled to 21,867 Hz (`CINEPAK.INC`);
  there is even a drift constant for synchronisation.
* Three separate volumes (sfx / background / CD) adjustable from the pause menu
  (`VOLUMES.TXT`).

## 2.9 Full-motion video (`CINEPAK.S`, `CINEDSP.DAS`, `CINELIST.GAS`)

* **Cinepak** codec in Atari's **FILM** container: `FILM`, `FDSC`, `CTAB`,
  `STAB` chunks, 64-byte header, `'1111'` sync.
* 320x240 at 16 bit, streamed straight off the CD in 2352-byte blocks.
* The player switches `VMODE` between RGB16 and CRY16 depending on the film.
* Porting note: this is the same format handled by FFmpeg's `segafilm`
  (`film_cpk`) demuxer, and Cinepak is supported everywhere. Nothing needs to be
  written from scratch.

## 2.10 Source files by role

**68000**
`HIGH1.S` (entry point, video init), `MAIN.S` (game loop, globals, tables),
`INTSERV.S` (interrupts), `VIDINIT.S` (NTSC/PAL), `JLISTER.S` (object list),
`CLEARJAG.S` / `ZAP.S` (memory clear), `JOY.S` (joypad, pause, volumes, cheats),
`GPU.S` / `LIBRARY.S` (GPU interface, `BlitZCopy`), `CDLOADER.S` (68k side of the
CD loop), `NVRAM.S` (saves), `CINEPAK.S` (FMV player), `OBJECT.S`, `SHEET.S`
(character sheets), `ITEMS.S`, `WORLD.S`, `INITTBL.S`, `LETTERS.S` (font),
`CBAR.S` (life bar gradient).

**GPU**
`KERNEL.GAS`, `3DENGINE.GAS`, `3DBITMAP.GAS`, `FORMMAT.GAS`, `ANIM.GAS` (which
includes `COLLIDE.GAS` and `FINDTRI.GAS`), `COMBAT.GAS`, `AICTRL.GAS`,
`EVENT.GAS`, `SCNLOGIC.GAS`, `SETLOGIC.GAS`, `SCENEPR.GAS`, `COLLECT.GAS` (plus
`CSHCODE.GAS`), `SHOWT.GAS` (text), `SCRIPT.GAS` (VM), `CDCONTRO.GAS` (CD
controller), `GPUCTRL.GAS`, `CINESTUB.GAS`, `ROOTER.GAS` (square-root test),
`ANDY.GAS` (an earlier version of `ANIM.GAS`, kept for reference).

**DSP**
`WAVE.DAS`, `JOYPAD.DAS`, `CINEDSP.DAS`.

**Disassemblies of Atari code** (not written by Lore)
`CDINIT.GAS`, `CDINITM.GAS`, `CDINITF.GAS`, `CINELIST.GAS` — debugger listings
(`Db: lg <address>`) of the CD BIOS and the Cinepak library.
