# pc-highlander

A native PC reimplementation of **Highlander: The Last of the MacLeods**
(Lore Design Ltd. / Atari Corp., Jaguar CD, 1995).

> **Status: phase 2 complete; the script VM and the audio are open.** All 672
> backdrops and their Z-buffers extract as PNG — the scene payload turned out to
> be XORed with an 8 KB key held in the game binary, read out of the shipped code
> with the new 68000 and Jaguar-GPU disassemblers. The models extract too, and
> the wine bottle from the disc is facet-for-facet the `MERLOT79.INC` in the 1995
> source. Also out: 327 animations, the 48 sets with their collision meshes and
> event lists, and the item text in three languages. Every backdrop can be named
> from the disc data. The game's puzzle and cutscene logic is bytecode for a GPU
> virtual machine, and all 1,173 commands of it now disassemble — which places 33
> of the 36 films. The **dialogue** turned out to be interleaved with the video:
> 18 minutes 23 seconds of it, plus 78 sound-effect bundles, all out as `.wav`.
> The world-state table and the character sheets are out too — they were static
> data in the binary all along. **Phase 2 is finished:** `tools/manifest.py`
> writes the single JSON the engine will read, tying 672 scenes, 48 sets, 197
> world records, 239 models, 327 animations, 36 films, 78 waves and every script
> together. Next is phase 3, a viewer.

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
| [docs/sessions/](docs/sessions/) | Work log, one note per session |

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
