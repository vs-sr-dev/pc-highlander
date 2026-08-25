# 01 — Inventory of the material

## 1.1 The source dump

Provenance: **"Contents of 1.44M floppy — Matt's Backup — 13 Jul 95"**
(`Info.txt`). It is a floppy backup of Matthew Jesson's workstation, not a tidy
export of the project repository.

Structure (identical in `Home/` and `Home2/`, verified with `diff -rq`: **the
two ZIPs are bit-identical**, same contents, same timestamps — one of them is
redundant):

```
DISK1/        6 files   - working files from the map editor
HIGH/       152 files   - the game's development tree
```

About **47,000 lines** of source in total.

| Extension | Files | Lines | What it is |
|---|---:|---:|---|
| `.S` | 20 | 9,296 | 68000 assembly (Motorola/Madmac) |
| `.GAS` | 26 | 23,109 | Jaguar **GPU** assembly (Tom, RISC) |
| `.DAS` | 3 | 754 | Jaguar **DSP** assembly (Jerry, RISC) |
| `.INC` | 26 | 12,570 | headers plus **data** (3D models, tables) |
| `.MAC` | 3 | 1,279 | macros (script VM, items, utilities) |
| `.SCT` | 3 | ~350 | game scripts (in-house language) |
| `.TXT` | ~45 | — | design notes, working memos, TODOs between developers |
| `.RGB`/`.TGA` | 4 | — | bitmaps (font, life bar, pause screen) |
| `.LIB` | 2 | 17 | Cinepak library stubs (68k and GPU) |

### File dates
December 1994 through **13 July 1995**. The game shipped in late 1995, so this
is a **work-in-progress** snapshot, not the shipping build. Important practical
consequence: the block offsets in `CDLINK.INC` (generated 4 July 1995) are
**not guaranteed** to match the retail disc — and phase 1 confirmed they do not
(see [06-jcd-format.md](06-jcd-format.md) §6.5).

### What is missing (and matters)

| Missing | Severity | Notes |
|---|---|---|
| `jaguar.inc`, `cd.inc`, `blit.inc`, `gpu.inc` | low | standard Atari Jaguar SDK headers, publicly available |
| **Map Editor / Map Tool** (M. Jesson) | medium | generates `CDLINK.INC`, `WORLD.INC`, the `.MAP` files and the CD layout |
| **SKELSKIN.EXE** (C. Lowe and M. Jesson) | medium | 3D Studio to Jaguar model converter |
| 43 of the 45 `.MAP` files | medium | only `DUN1.MAP` and `DUN2.MAP` are present |
| 3D Studio scenes (`.3DS`) for the backdrops | high | backdrops are rendered offline; recoverable only from the CD |
| **all binary game data** | — | it lives on the CD (see [doc 04](04-cd-and-assets.md)) |
| the script compiler (`.SCT` to bytecode) | medium | but the language is fully documented in `OPCODES.INC` plus `SCRIPT.MAC` |

### Design notes included (a goldmine)
`NOTES.TXT`, `CACHE.TXT`, `VOLUMES.TXT`, `PCOL.TXT`, `SMOOTH.TXT`, `SAVE.TXT`,
`NVRAM2.TXT`, `COLLIDE*.TXT`, `COMBAT*.TXT`, `SCRIPT*.TXT`, `THOUGHTS.TXT`,
`TODO.TXT`, `BUGSNOTE.TXT` — plain-English descriptions of the collision,
combat, stair-climbing, scene-caching and save-game algorithms. Read these
before rewriting the corresponding modules.

---

## 1.2 The two disc images

| File | Size | Verdict |
|---|---:|---|
| `Highlander - The Last of the MacLeods (USA).jcd` | 456,126,464 B | **good** |
| `Highlander.jcd` | 427,819,008 B | **damaged — discard** |

### Why the second one is unusable
Scanned byte by byte: of 427 MB, only **22.3 MB are non-zero**, spread across 12
runs of exactly 2 MB each, aligned to 2 MB boundaries:

```
0x02A00000..0x02C00000   0x03600000..0x03800000   0x05A00000..0x05C00000
0x06A00000..0x06C00000   0x09000000..0x09200000   0x0E600000..0x0E800000
0x11400000..0x11600000   0x13C10000..0x13E00000   0x17600000..0x176A0000
0x176B0000..0x17800000   0x18600000..0x18800000   0x19600000..0x19800000
```

That pattern — only the last buffer of each read window ever written — is the
signature of a failed rip. It also has no `JCD` header and no Jaguar CD boot
signature. **The two images are not two versions: one is simply corrupt.**

### The good image
* Container header: ASCII magic `JCD\0` at offset 0, followed by a table of
  **nine 12-byte records**, one per track. Fully decoded in
  [06-jcd-format.md](06-jcd-format.md).
* One audio track plus eight data tracks, 193,935 blocks total.
