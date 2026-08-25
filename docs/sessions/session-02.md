# Session 2 — Phase 1: opening the disc

**Goal:** decode the `.jcd` container, map the tracks, and check whether the
`DATA.INC` offsets (July 1995) still hold on the retail disc.

## Done

* **`.jcd` container fully decoded:** 12-byte header, a table of nine 12-byte
  records, track offsets in units of 512 bytes.
* **Found the data encoding:** every long is stored byte-reversed. This was the
  step that unlocked everything — without it the content looks like noise. Found
  by spotting readable-but-jumbled text and reconstructing it until it came out
  as clean French.
* **Data track structure identified:** 64 bytes of `ATRI` lead-in, 32 bytes of
  `"ATARI APPROVED DATA HEADER ATRI"` plus a type byte, 64 bytes of content tag,
  then the payload. The type byte is `0x20 + track index`, i.e. the game's *data
  type*.
* **Retail disc layout mapped:** one audio track plus eight data tracks.
  Track 4 (`PICT`, 166 MB) = scenes; track 7 (`1111`, 229 MB) = Cinepak;
  track 6 = audio samples.
* **Zero point verified:** `BO_CINEPAK_BUZZ = 0` lands exactly on the first
  film's `FILM` header. The formula
  `offset = track_start + 160 + block * 2352` is confirmed to the byte.
* Wrote [tools/jcd/jcdinfo.py](../../tools/jcd/jcdinfo.py): lists tracks,
  extracts them de-swapped, hex-dumps them.

## How close is the July code to the shipped game?

**On the data side, less than we hoped.** Three findings:

1. **Data types went from 13 to 8.** Categories were merged between July and
   October.
2. **Films went from 20 to 36.** All `cvid` 320x240. Only the first one kept its
   block offset; every other one moved.
3. **The Red Book speech track is gone.** In July, 88 spoken lines lived on a
   dedicated audio track (up to block 26,653). On the retail disc the only audio
   track is 10,472 blocks long: far too short. The speech is elsewhere.

On top of that, the retail disc carries **French and German** text, of which the
July source shows no trace. It became a multilingual game after this snapshot.

**The engine documentation, however, still holds.** What changed is the
*packaging* of the data, not the architecture: `GetTrack` behaves exactly as
described in the source, blocks are 2352 bytes, the Cinepak FILM format matches
`CINEPAK.INC`, and track 7's tag is literally the `sync_header equ '1111'`
declared in the source. The July code is a reliable **engine specification**;
its offset tables are not.

## Open

1. Identify tracks 3, 5, 8 and 9 (generic `DATA`).
2. Confirm the scene size: 73,921 / 110 = 672.009, so "672 scenes of 110 blocks"
   is very likely but needs visual confirmation.
3. Rebuild the name-to-film mapping for the 36 Cinepaks: the 20 names in
   `DATA.INC` are in alphabetical order, which helps but is not enough.
4. Work out where the speech went.
5. Settle the framebuffer encoding (CRY vs RGB16 with VARMOD) on the backdrops.

## Next session

Phase 2: extractors. Scenes first — they are the bulk of the content and the
part that makes the game recognisable at a glance. First milestone: one backdrop
opened as a PNG with its Z-buffer alongside.
