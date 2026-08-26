# pc-highlander

A native PC reimplementation of **Highlander: The Last of the MacLeods**
(Lore Design Ltd. / Atari Corp., Jaguar CD, 1995).

> **Status: phase 3 complete, phase 4 under way.** `src/` builds
> `hlview`: an SDL3 window over a 320x200 RGB16 framebuffer that opens any of
> the 672 backdrops by name, shows its Z-buffer, spins a model read straight
> off the disc, and **composites that model into the scene, depth-tested against
> the backdrop's own Z half.** A wine bottle standing on a tent floor keeps
> every one of its pixels in the open, 36 of 77 with the tent pole across it,
> and none at all behind the pole.
>
> **And it now walks a character around a set.** Fifteen pieces off track 5,
> chained through the origin points that turn out to be the skeleton, posed by
> an animation frame, standing on the collision mesh and cutting the camera when
> it crosses one of the set's `SCENE` events — which is phase 4's success
> criterion ([docs/14-characters.md](docs/14-characters.md)). The assembly is
> checked against something it never reads: every animation frame records the
> highest and lowest point of the pose it describes, and reproducing both for
> all **6,591 character frames on the disc** costs a mean error of 2.5 units on
> a figure 414 units tall.
>
> Drawing settled the three conventions no amount of reading could
> ([docs/13-viewer.md](docs/13-viewer.md)): the camera footer's matrix is **row
> major over a y-up world**, the Z-buffer stores **`65536 - |z|`** so nearer is
> larger, and the item models are stored on their side with the **world records
> standing them up** at elevation 192 on the game's 256-step circle.
>
> **Phase 4 has started.** The engine loads a set off track 3 — views, doorways,
> events and the floor mesh — and draws that mesh over the backdrop, which is
> the debugging view the rest of the phase gets built against. The triangle
> search is `FINDTRI`'s, a search over a list rather than a walk down one path,
> and the difference matters: greedy fails on 258 of the disc's 5,342 triangles,
> the list search on none of them. Movement over that mesh is `COLLIDE.GAS`'s:
> a swept circle against the edges it can reach, a wall or a step over 255 units
> stops it dead, and the floor underfoot is the highest any reached triangle
> offers.
>
> **And the pad now drives him, through doorways, into other sets.** The
> joypad picks the animation and the animation's own root motion does the
> moving, which is `AICTRL.GAS`'s arrangement and not a viewer's: a release of
> forward for under three frames does not stop the walk, a press inside that
> window is a double tap and becomes a run, and what a button does depends on
> the stance the character is already in. When a `SCENE` event names a view
> another set owns, the engine loads that set and stands him at the arrival its
> init table names — which needed §10.3 read the right way round. The id is the
> view you are **leaving**, not the one you arrive at: not one of the disc's 153
> entries is keyed on its own group, and for all 123 with a real id the
> departing set has an event, fired from that very view, cutting into the
> arriving one. The flags word is `facing * 256 + character`, for all 153, and
> the `.MAP` files say the same thing in words — two `START` blocks, `QUENTIN`
> and `RAMIREZ`, sharing one `FROM`, each with an `ORIENTATION` in degrees.
> `hlview --check-doors` runs it over the whole disc, and found a bug that had
> nothing to do with doorways: a triangle's vertex index was being read into a
> byte, and three of the meshes have more than 255 vertices.
>
> **The ground gap turned out to be a measurement, not a mystery.** Session 8's
> per-set table — 348 units in `PRI`, 155 in `CA` — was an artefact of taking the
> modal height over a collision triangle's whole plan footprint, walls and crates
> included; on the same triangle from two cameras that estimate disagrees with
> itself by a median of 300 units. Measured as the lowest flat surface a camera
> sees, and again by the engine itself raising a model until the backdrop stops
> hiding it, the residual is **local relief in the art of a few tens of units** —
> six or seven pixels on a 320x200 screen. The surviving `.MAP` files say why
> there was never a correction to find: the map editor calibrates the horizontal
> plane and nothing else, and every ground height in the game is an integer
> somebody typed in by eye.
>
> Two smaller things the disc gave up on the way. The camera footer's long at
> +32 is not the constant session 3 took it for: it is the **set's own block on
> track 3**, which names the set a view belongs to outright, and it checks out
> on all 672. And **no facet on the disc carries a normal** — all 6,821 are
> zero — which means the engine's own backface test always passes and the
> Z-buffer does the work. The viewer no longer culls.
>
> Behind that, phase 2: all 672 backdrops and their Z-buffers, the models — the
> disc's wine bottle is facet-for-facet the `MERLOT79.INC` of the 1995 source —
> 327 animations, the 48 sets with collision and events, the **script VM** with
> all 1,173 of its commands, 18 minutes 23 seconds of **dialogue** recovered
> from inside the Cinepak films, 78 sound-effect bundles, the item text in three
> languages, and the **world state** and **character sheets**, which were never
> on the disc at all but static tables in the binary.
> [tools/manifest.py](tools/manifest.py) ties it all into the single JSON the
> engine reads. One 156 KB track is still unidentified and nothing depends on it.

---

## BYOA — Bring Your Own Assets

This repository does **not** contain, and never will:

* the original 1995 source code (© Lore Design Ltd. / Atari Corp.);
* disc images (`.jcd`, `.cdi`, `.bin`/`.cue`) of the game;
* assets extracted from the disc (models, animations, backdrops, FMV, audio, scripts).

It contains only:

* **documentation of data formats** — facts, not creative expression;
* **our own code** — a from-scratch engine and extraction tools;
* not a single byte copied from the original material.

Playing will require a copy of the Jaguar CD disc that you legitimately own.
The expected local layout (all of it in `.gitignore`):

```
PC-Highlander/
  Highlander/          <- 1995 source dump + disc images (local, never committed)
  assets/              <- output of the extraction tools (local, never committed)
  docs/  src/  tools/  <- this repository
```

## Licence

[MIT](LICENSE), covering this repository's own contents — the tools, the engine
and the format documentation. It does not cover *Highlander: The Last of the
MacLeods*, whose code, data and assets remain with their rights holders and are
not distributed here.

---

## The game in one line

A third-person 3D action/adventure built on **pre-rendered fixed-camera
backdrops** (*Alone in the Dark* / *Resident Evil* style), with real-time
polygonal characters composited into the scene using a **pre-computed
Z-buffer**, plus **Cinepak** full-motion video and **Red Book** CD audio.

## Documentation

| Doc | Contents |
|---|---|
| [docs/01-inventory.md](docs/01-inventory.md) | What is in the source dump, what is missing, the state of the disc images |
| [docs/02-architecture.md](docs/02-architecture.md) | Engine architecture: 68000 + GPU + DSP, game loop, memory map |
| [docs/03-data-formats.md](docs/03-data-formats.md) | Data structures and file formats |
| [docs/04-cd-and-assets.md](docs/04-cd-and-assets.md) | How the game addresses the CD, and the asset extraction plan |
| [docs/05-roadmap.md](docs/05-roadmap.md) | Porting strategy and phased roadmap |
| [docs/06-jcd-format.md](docs/06-jcd-format.md) | The `.jcd` container format and the retail disc layout |
| [docs/07-scene-format.md](docs/07-scene-format.md) | Scene format: slot layout, pixel format, the XOR obfuscation |
| [docs/08-code-and-gpu.md](docs/08-code-and-gpu.md) | Inside the retail binary: boot chain, memory map, GPU modules |
| [docs/09-text-and-fmv.md](docs/09-text-and-fmv.md) | The localised text and the Cinepak films |
| [docs/10-set-track.md](docs/10-set-track.md) | The set track: scene tables, doorways, collision, events |
| [docs/11-script-vm.md](docs/11-script-vm.md) | The script VM: encoding, opcodes, and what the scripts do |
| [docs/12-world-and-sheets.md](docs/12-world-and-sheets.md) | The world-state table and the character sheets |
| [docs/13-viewer.md](docs/13-viewer.md) | The viewer: the view transform, the Z-buffer convention, the rasteriser, the floor |
| [docs/14-characters.md](docs/14-characters.md) | The character: the skeleton, the pose, movement over the mesh, the camera cuts |
| [docs/sessions/](docs/sessions/) | Work log, one note per session |

## The engine

```
make                                    # -> build/hlview, needs SDL3
build/hlview --scene CA_CAM03           # a backdrop, by name
build/hlview --scene CA_CAM03 --depth   # its Z-buffer
build/hlview --model boot:6 --spin      # the wine bottle, turning
build/hlview --scene TENT6_CAM01 --model boot:6 --object '#190'
build/hlview --scene DUN1_CAM00 --mesh  # the collision mesh over the art
build/hlview --check-mesh               # the triangle search, checked
build/hlview --char 0 --anim 10 --play  # Quentin, walking
build/hlview --scene DUN1_CAM04 --char 0 --anim 10 --walk --events
build/hlview --scene DUN1_CAM04 --char 0 --drive        # the arrows drive him
build/hlview --check-char               # the pose, against every frame's own extent
build/hlview --check-doors              # the doorways, over the whole disc
```

More in [src/README.md](src/README.md).

## Tools

```
python tools/jcd/jcdinfo.py <image.jcd>                     list tracks
python tools/jcd/jcdinfo.py <image.jcd> --extract DIR       extract de-swapped tracks
```

`--extract` writes one file per track, de-swapped and with the header stripped.
Everything below works on those files.

```
# all 672 backdrops and Z-buffers as PNG
python tools/scene/scenex.py DIR/track04_pict.bin --boot DIR/track02_00004000.bin --out assets/scenes --depth

# a backdrop's Z-buffer, inverted back into world coordinates
python tools/scene/backproj.py DIR/track04_pict.bin --boot DIR/track02_00004000.bin --check DUN1
python tools/scene/backproj.py DIR/track04_pict.bin --boot DIR/track02_00004000.bin --ground D1

# the item text, English / French / German
python tools/text/textx.py DIR/track02_00004000.bin --format tsv

# the models: 19 in the binary (the items), 220 on track 5 (characters)
python tools/model/modelx.py DIR/track02_00004000.bin --obj assets/models --png assets/models

# the animations: 285 on track 5, 42 on track 8
python tools/anim/animx.py DIR/track05_data.bin --json assets/anims.json

# the 48 sets: scene tables, doorways, collision meshes, events
python tools/set/setx.py DIR/track03_data.bin --json assets/sets.json

# the world state: 197 objects and characters, and 40 character sheets
python tools/world/worldx.py DIR/track02_00004000.bin --json assets/world.json

# the 36 Cinepak films, and the audio interleaved in them
python tools/cinepak/filmls.py DIR/track07_1111.bin --tsv
python tools/cinepak/filmwav.py DIR/track07_1111.bin --out assets/filmaudio

# the 78 sound-effect bundles
python tools/wave/wavex.py DIR/track06_data.bin --out assets/waves
python tools/wave/wavex.py DIR/track05_data.bin --out assets/waves

# the scripts: 27 sets plus the resident MAINSCRIPT
python tools/script/scriptx.py DIR/track03_data.bin --all --out assets/scripts
python tools/script/scriptx.py DIR/track02_00004000.bin --main

# everything above, cross-referenced, in one JSON
python tools/manifest.py DIR --out assets/manifest.json

# the game code, and one GPU module
python tools/m68k/dis68k.py DIR/track02_00004000.bin --off 0x12600 --base 0x5000 --len 0x30000 --entry 0x5000 --out code.asm
python tools/gpu/disgpu.py DIR/track02_00004000.bin --off 0x36e98 --header
```

## Credits for the original

Lore Design Ltd., 1994-95 — Andrew M. Harris (3D engine, CD, events),
Robert C. Dibley (animation, combat, script VM, NVRAM),
Matthew Jesson (map/asset tools, game scripts), Jakes Mo (audio, logic,
Cinepak), Chris Lowe (model tools). Published by Atari Corp.
