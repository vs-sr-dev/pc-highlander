# Session 1 — Analysis

**Goal:** understand the material. No code.

## Done

* Inventoried the source dump: a 1.44 MB floppy, "Matt's Backup", 13 July 1995.
  158 files, about 47,000 lines.
* Verified that `Home/` and `Home2/` (and the two corresponding ZIPs) are
  **bit-identical**: one of them is pure duplication.
* Reconstructed the engine architecture: 68000 as orchestrator, GPU running all
  of the game logic in 4 KB overlays, DSP for audio.
* Documented the main data structures (WST / ACT / CIT / DDA, character sheets,
  models, animations, sets, collision mesh, `.MAP` format).
* Documented the script language and its 60+ opcodes in full.
* Reconstructed the CD addressing scheme: `(data type, block offset)`, with the
  data type used as a track index relative to the second session.
* **Solved the mystery of the two disc images:** `Highlander.jcd` (427 MB) is a
  corrupt rip — 22 MB of real data out of 427, in 12 aligned 2 MB runs, the rest
  zeros, no `JCD` header. Use only the 456 MB USA image.
* Created the `pc-highlander` repository with a strict BYOA policy.

## Decided

* C99 + SDL3 + CMake, software rasteriser.
* Fidelity to the original first, improvements as toggles.
* The 1995 source stays local, outside the repository.

## Open

1. The `JCD` container header is not yet decoded with certainty.
2. Scene format (110 blocks): the split between backdrop, Z-buffer and any
   header is unknown.
3. CRY vs RGB16 with VARMOD: to be determined experimentally.
4. The missing Atari SDK headers (`jaguar.inc`, `cd.inc`, `blit.inc`, `gpu.inc`)
   would be useful as a reference for hardware constants.

## Next session

Phase 1: open the `.jcd` container, map the tracks, and check whether the
`DATA.INC` offsets (4 July 1995) still hold on the retail disc.
