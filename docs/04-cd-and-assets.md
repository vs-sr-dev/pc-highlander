# 04 — CD addressing and the asset extraction plan

## 4.1 How the game addresses data

The Jaguar CD has **no filesystem**: reads are by absolute MSF timecode.
Highlander builds a two-level addressing scheme on top:

```
(data type, block offset)  ->  MSF timecode  ->  CD_read
```

* The **data type** is a **relative track index**. The comment in
  `CDCONTRO.GAS` says it outright:
  `;N.B. TRACK OFFSET FOR DATA == DATA TYPE`
* The **block offset** is the offset in blocks from the start of that track.

The conversion happens in `GetTrack` (`CDCONTRO.GAS`, around line 2210):

1. walk the CD BIOS **TOC** (`CD_toc`), skipping the first record, until the
   first byte of a record's second long equals 1 — i.e. until it finds the
   **first track of the second session**;
2. `record = TOC_base + data_type * 8`, from which it reads that track's MSF
   timestamp;
3. convert MSF to blocks: `min*60*75 + sec*75 + frame`;
4. add the requested block offset;
5. subtract a **fudge factor of 4** blocks (the comment cites the 150-block
   silence and a 6-block pre-read);
6. convert back to MSF and hand it to `CD_read`.

**Practical consequence for the port:** all we need, per data type, is the
**absolute starting sector** of the corresponding track. From there every `BO_*`
constant in `DATA.INC` and `CDLINK.INC` becomes a direct index. Phase 1 resolved
this — see [06-jcd-format.md](06-jcd-format.md).

## 4.2 The CD queue

`CDCONTRO.GAS` (GPU) and `CDLOADER.S` (68k) implement a 64-entry circular queue
(1 KB, `cdq`), 16 bytes per entry:

```
cdqState     .w   UNLOADED $01 / LOADING $02 / LOADED $04 / PROCESSED $08
                  WAIT $10 / PRIORITY_WAIT $80
cdqTrack     .w   = data type
cdqBlock     .l   block offset
cdqBuffer    .l   (scenes only)
cdqBufferPtr .w   1 = a, 2 = b, 3 = c
cdqEntry     .w   position in the sheet, for post-processing
cdqSheet     .w   sheet in which to record the reference
cdqReserved  .w
```

Two pointers (`currcd`, `entercd`) chase each other around the ring. Requests
can be promoted to *priority wait* when a scene is needed immediately.

The `cdloader` also handles switching between **data mode** (`CD_mode` 3, double
speed) and **Red Book** (`CD_mode` 0, single speed plus `CD_jeri`).

## 4.3 The offset tables we already have

`DATA.INC` and `CDLINK.INC` are files **generated** by the Map Tool on 4 July
1995, containing every symbolic offset:

| Table | Entries | Example |
|---|---:|---|
| `BO_LOGICS_*` | 5 | `BO_LOGICS_1 = $0` (full), `BO_LOGICS_3B = $40` (stand only) |
| `BO_MODEL_*` | 26 | `BO_MODEL_QUENTIN = $0`, step $10 |
| `BO_ANIM_*` | 38 | includes the "forced" cutscene animations |
| `BO_SOUND_*` | 25 | footsteps per surface, ambiences, vocalisations |
| `BO_CDAUDIO_LINE*` | 88 | spoken dialogue, `LINE005 = 150` ... `LINE100 = 26653` |
| `BO_CINEPAK_*` | 20 (+6 aliases) | `BUZZ 0`, `TITLES 83298`, `TURRET 89842` |
| `BO_SET_*` | 45 | step $10 |
| `BO_SCENE_*` | 594 | step $6e = 110 blocks per scene |
| `SCENE_*` | 594 | logical scene number, not an offset |

**110 blocks per scene** is the fixed size of a backdrop with its Z data.

> **Status after phase 1:** the *base* is confirmed exactly
> (`BO_CINEPAK_BUZZ = 0` lands on the first film's `FILM` header, to the byte),
> but the rest of the July offsets no longer match the retail disc. Treat these
> tables as a catalogue of **names and of what exists**, not as a map.

## 4.4 The disc image

Use **only** `Highlander - The Last of the MacLeods (USA).jcd` (456 MB). The
other one is a corrupt rip (see [01-inventory.md](01-inventory.md) §1.2).

The container format, the byte-swap, the track structure and the retail layout
are all documented in [06-jcd-format.md](06-jcd-format.md).

## 4.5 Extraction plan

Goal: go from "opaque disc image" to "assets extracted and viewable".

1. ~~Decode the `.jcd` header, map data type to track to sector.~~ **Done**,
   phase 1.
2. ~~Validate the `DATA.INC` offsets.~~ **Done** — the base is exact, the
   individual offsets are not; they must be rediscovered by signature scanning.
3. **Scene extractor** — work out the 110-block layout, pull out backdrop and
   Z-buffer, write them as PNG (image) and 16-bit PNG / PGM (depth).
4. **Cinepak extractor** — the easiest: FILM container, decode with FFmpeg for
   visual verification. 36 films located already.
5. **Model extractor** — validated against the 11 models present in the clear in
   the source (`MERLOT79.INC` and friends): parsing the wine bottle off the CD
   must reproduce the same vertices.
6. **Set extractor** — collision, events, start points, scene table.
7. **Animation, sheet, sound extractors.**

Each extractor lives in `tools/`, writes into `assets/` (git-ignored) and emits
a JSON manifest using the names from `CDLINK.INC` / `DATA.INC` / `WORLD.INC`.
