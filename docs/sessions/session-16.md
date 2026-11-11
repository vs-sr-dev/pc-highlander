# Session 16 — The save code, and the port onto the kit

Two things this session. The repository's first issue got its answer, and with
it a chapter. Then the port moved onto jaguarkit, engine and tools, with every
check printing what it printed before.

→ [18-memory-track.md](../18-memory-track.md), [17-jaguarkit.md](../17-jaguarkit.md)

---

## 1. Issue #1: the Memory Track

hyjinx187 had mapped the retail binary's save code while chasing the RetroHQ
GameDrive's Load/Delete freeze. The issue waited two months, because
notifications had never been on for any of the account's repositories. They
are now, for all 260.

Every address held against the bytes. The code around them adds the menu's
dispatch. LOAD and DELETE list the slots by opening each `GAME n` while
SETTINGS is still open, and close each one that opens. SAVE lists them
without a call. Only DELETE OTHER uses SearchFirst/SearchNext. Boot never runs
that listing, and boot's own sequence runs again in game, with interrupts on,
every time the pause screen opens. SAVE's 7,040-byte write also works with
interrupts on. So interrupts are common to what works and to what freezes,
and the question moves to the second handle. That is
[18-memory-track.md](../18-memory-track.md), credited to them.

Two things the 1995 `NVRAM.S` does not have: the chunk marker is `$99` (it was
`$CF`), and the first chunk carries two longs a GPU routine computes. If they
do not match after a load, the game shows `FAILED`, deletes the save and
restarts.

## 2. A decision: no recompiler, here

Every sibling kit has a static recompiler, so the question was whether this
port should become one. It should not. About 70% of the 1995 source is GPU
code (15,474 lines of `.GAS` to 6,432 of `.S`), loaded in pieces into 4 KB:
the 3D engine, the script machine, the AI, combat, collision. A 68000
recompiler would get the menus, the CD queue and the saves. The README and
jaguarkit's own now say so. The kit's recompiler waits for a Jaguar game that
needs one.

## 3. The engine's films are the kit's

`src/media/film.c` and `cinepak.c` are gone. The engine builds
`jaguarkit/runtime/jk_film.c` and `jk_cvid.c`, and its calls change name and
nothing else. With every check's output saved first, all eight print the same
bytes after.

That held only with track 7 as the kit extracts it. On `jcdinfo.py`'s file,
which begins 160 bytes into the track, the kit's block rule (where the sync
pad starts) put films 10 and 35 one block early. The scripts say 38,406 and
100,863.

Then the rest of the engine followed. Taking every track from the kit broke
six checks, but not because the readers were wrong. The kit's file is
`jcdinfo.py`'s with 160 bytes in front: the `ATRI` lead-in, the Atari header
and the sync pad. Past those the bytes are the same, on every data track. So
`io_read_track` reads a kit track from `TRACK_ORIGIN` = 160, and every offset
stays where it was, the manifest's included. `hlview --tracks` now means the
kit's extraction (default `assets/kit_tracks`). All eight checks print the
same bytes, and so do the first, the last and one more of the 672 backdrops,
which no check reads.

## 4. And the tools

The extractors read kit tracks through `tools/track.py`, from the same
origin. Old tools on `jcdinfo.py`'s files and new ones on the kit's write the
same 150 files and the same output. The manifest is the same but for its
`disc` section, with its films now from `jaguarkit.film`. The committed
manifest was older than the tools on one point: film 17's speech is 411,600
bytes, not August's 433,820. `tools/jcd`, `m68k`, `gpu`, `cinepak` and most
of `emu` are gone, replaced by the kit. `cmpfb.py` stays, decoding with the
kit and printing what it printed on session 14's dump. The engine's decoder
and the kit's Python one still agree byte for byte on film 19 frame 30 and
film 6 frame 400.

So the port is on the kit, engine and tools, and jaguarkit is one game's kit
in fact as well as in name.

## TODO for session 17

Session 15's list stands, less item 3, which this session finished:

1. **The way in**: power-on to the first playable frame and back
   (session 15, item 1).
2. **Sound** (session 15, item 2).
3. **What the player sees.** For the saves, 18.2 now says what the original
   keeps: settings, game state, the scene, and the world table.
4. **The probe on the game**, then 5. **the playthrough**, as before. The
   probe's dumps now come from `jaguarkit/bigpemu/jk_ramdump.c`.
