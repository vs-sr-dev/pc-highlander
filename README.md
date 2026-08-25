# pc-highlander

A native PC reimplementation of **Highlander: The Last of the MacLeods**
(Lore Design Ltd. / Atari Corp., Jaguar CD, 1995).

> **Status: phase 1 complete.** The `.jcd` container format is decoded and the
> retail disc layout is mapped. Engine work has not started yet.

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
| [docs/sessions/](docs/sessions/) | Work log, one note per session |

## Tools

```
python tools/jcd/jcdinfo.py <image.jcd>                 list tracks
python tools/jcd/jcdinfo.py <image.jcd> --extract DIR   extract de-swapped tracks
python tools/jcd/jcdinfo.py <image.jcd> --hex 7 0 256   hex dump a track
```

## Credits for the original

Lore Design Ltd., 1994-95 — Andrew M. Harris (3D engine, CD, events),
Robert C. Dibley (animation, combat, script VM, NVRAM),
Matthew Jesson (map/asset tools, game scripts), Jakes Mo (audio, logic,
Cinepak), Chris Lowe (model tools). Published by Atari Corp.
