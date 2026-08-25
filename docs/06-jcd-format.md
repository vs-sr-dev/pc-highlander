# 06 — The `.jcd` container and the retail disc layout

Reverse-engineered from the USA disc image. Reference implementation:
[tools/jcd/jcdinfo.py](../tools/jcd/jcdinfo.py).

---

## 6.1 Container header

```
offset  size  field
0x00     4    magic "JCD\0"
0x04     3    reserved (00 00 01)
0x07     1    track count
0x08     4    02 2B 1D 1D   (lead-out, 43:29:29)
0x0C    12*N  track table
```

Track record, 12 bytes:

```
+0   1   track number (1-based)
+1   3   start MSF          min, sec, frame - raw binary bytes, not BCD
+4   1   flag: 0 = audio, 1 = data
+5   3   length MSF         same encoding
+8   4   data offset in the file, in units of 512 bytes (big endian)
```

The offset field must be **multiplied by 512**. The start MSF values include the
pregaps (~152 blocks between tracks), which are **not** stored in the file: to
navigate the file use the offset field only, never the MSF.

## 6.2 The key finding: longs are byte-reversed

**All data-track content is stored with every 4-byte group in reverse order.**
Without undoing this the content looks like noise with scrambled fragments of
text in it.

```c
for (i = 0; i + 3 < n; i += 4) {
    swap(buf[i], buf[i+3]);
    swap(buf[i+1], buf[i+2]);
}
```

How it was found: around `0x17A5A7E` there was readable but jumbled text
(`...LEROK ~NATRIAFNU EOR E...`). Reversing each long yields
`... A KORTAN~FAIRE UNE RONDE A L'EXTERIEUR...` — clean French.

> Side effect worth knowing: the `ATRI` signature you find by naively searching
> the file is **not the real one** — it is a coincidence at an unaligned offset.
> The real signature, once longs are reversed, sits at `0x177DC00`, which is
> exactly where track 2's data begins (`48110 * 512 = 24,632,320`). That was
> also the final confirmation of the 512-byte offset unit.

The text found is **French and German** — in-game item descriptions. The USA
disc is multilingual, something the July source shows no sign of.

## 6.3 Data track structure

```
+0     64   lead-in: "ATRI" repeated 16 times
+64    32   "ATARI APPROVED DATA HEADER ATRI" plus 1 type byte
+96    64   content tag, 4 characters repeated 16 times
+160   ...  payload
```

The **type byte** equals `0x20 + data track index`: it is precisely the *data
type* the game passes to `GetTrack` (`CDCONTRO.GAS`), which uses it as a track
offset relative to the first track of the second session.

Therefore **block offset 0 of a data type corresponds to `payload`**, i.e.
`track_start + 160`. Verified: `BO_CINEPAK_BUZZ = 0` lands exactly on the `FILM`
header of the first film, to the byte.

## 6.4 Retail disc layout (USA)

| tr | tag | code | blocks | MB | contents |
|---:|---|---|---:|---:|---|
| 1 | AUDIO | — | 10,472 | 23.5 | audio track (16-bit PCM) |
| 2 | — | `0x20` | 120 | 0.3 | boot: the complete resident game binary |
| 3 | `DATA` | `0x21` | 2,689 | 6.0 | **sets** — 48 slots of 56 blocks |
| 4 | `PICT` | `0x22` | 73,921 | 165.8 | **scenes** — 672 slots of 110 blocks |
| 5 | `DATA` | `0x23` | 1,849 | 4.1 | **models, animations and waves** — 33 slots of 56 blocks |
| 6 | `DATA` | `0x24` | 2,129 | 4.8 | **audio samples** — 38 `WAVE` bundles, 56-block slots |
| 7 | `1111` | `0x25` | 102,127 | 229.1 | **Cinepak** — 36 films, `cvid` 320x240 |
| 8 | `DATA` | `0x26` | 561 | 1.3 | **animations** — 10 slots of 56 blocks |
| 9 | — | `0x27` | 67 | 0.2 | 55 KB of entropy-8 data, then filler — identity open |

Tracks 3, 5, 6 and 8 all use a **uniform slot stride of 56 blocks**
(131,712 bytes), the same way the scene track uses 110. Slot counts: 48, 33, 38
and 10 respectively.

Track 2's "tag" is not text at all — the four bytes are `00 00 40 00`, the load
address, and the track holds a small boot loader followed by the game itself.
See [08-code-and-gpu.md](08-code-and-gpu.md).

### What is in tracks 5 and 8

Both hold records that match the July structures byte for byte, each record
preceded by a long holding its length, each slot starting a new record:

* **models** — the `SKELSKIN` header of §3.3. Self-consistent throughout: `VLP`
  is always `$4018`, i.e. the header plus 24 bytes at a load base of `$4000`,
  and `FLP - VLP` equals `(vertices + origins) * 8` in every record checked.
  **220 on track 5**, fifteen per character slot.
* **animations** — the `ANIM` header of §3.4, at offset 4. Every record
  satisfies `size = 14 + framesize * frames` with `framesize = 20 + 3 * models`
  rounded up to even, which for the fifteen pieces every character has means 66.
  20 fps throughout. **285 on track 5 and 42 on track 8**, 570 seconds in total.
* **waves** — `long total; 'WAVE'; long size; signed 8-bit samples`, where
  `total` is `size + 8` rounded up to a multiple of four. All 38 slots of
  track 6 hold one record each; on track 5 the **odd** slots 1 to 17 and slot 28
  hold **four each**, which is the four combat sounds `STRUCDEF.INC` gives a
  character — `soundKIA`, `soundHIT`, `soundATT`, `soundPAR`. 78 waves in all,
  98.7 seconds. See [tools/wave/wavex.py](../tools/wave/wavex.py).

Fifteen models per character and fifteen animated pieces per animation is the
same number seen from two sides, which is a good cross-check on both readings.

So track 5 is a **per-entity bundle**: a character slot holds its fifteen models
and then its animations, one after the other, with the sounds in the neighbouring
slot. The seam is exact — in slot 0 the last model ends at `$1CAC` and the first
animation begins at `$1CAC` — and across the whole track the 220 model records
and the 285 animation records do not overlap anywhere, which is the strongest
check there is that both readings are right.

### Track 9

156,512 bytes, of which only the **first 55,188 are real**: the rest is the long
`$C00DADE0` repeated 25,187 times, then the `ATARI APPROVED DATA TAILER`. The
real part measures 7.996 bits/byte and shows no repeating-key structure — every
shift from 1 to 20,000 was tested, peak coincidence 0.0051 against a 0.0039
baseline — and it is not XORed with the scene key. Its 64-byte content tag is
high-entropy as well, so even the tag is scrambled. It is compressed or
encrypted; no code asking for type `$27` has been found yet.

Session 5 added four results, all negative, and one that reframes the search:

* **The track header is intact; only the tag and the payload are scrambled.**
  The 64-byte `ATRI` lead-in and the 32-byte `ATARI APPROVED DATA HEADER` with
  its type byte read normally on the disc. Whatever happened to this track began
  at offset 96, where the 4-character content tag should be.
* **The tag is not a plaintext tag under a byte-wise cipher keyed on the game
  binary.** Every other track's tag is one 4-byte value repeated sixteen times.
  For a byte-wise XOR or additive cipher that gives a known relation that does
  not need the tag's value: `ct[i] op ct[i+4]` must equal `k[i] op k[i+4]` for
  the first 60 bytes. No 64-byte window anywhere in the 282,240-byte resident
  binary satisfies it, under either operation; the best partial match is 5 bytes
  out of 60, which is chance.
* **The payload is unique on the disc** — a 24-byte sample of it occurs exactly
  once in the whole 456 MB image, so it is not a stale copy of another track's
  data.
* Its statistics are those of random data: chi-square 291 on 255 degrees of
  freedom, and **not one 4-byte sequence repeats** in 55,185 overlapping
  positions. So the plaintext, if there is one, is not the sparse, zero-filled
  kind the other data tracks hold.

The search for the loader should probably be reframed. `sub_007E9E`, the routine
that turns a data type into a track, has **exactly one call site** in the whole
binary — `$866A`, asking for type 5, the Cinepak track. Every other read on the
disc goes through the block number in `$4494` with a base computed elsewhere, so
there is no "ask for type 7" call to find; the question is which code path could
ever compute a block inside track 9, and none has turned up.

Entropy per data track: track 3 measures 0.28 bits/byte (97% zero fill), track 5
3.23, track 6 3.20, track 8 1.84 — all plainly raw. Track 4 measures 7.81 to
7.88 because the backdrops are XOR-obfuscated (§7.3). Track 9's figure of 5.03
is an average diluted by its filler; its real payload measures 7.996.

193,935 blocks total, about 456 MB on a ~700 MB disc.

The scene stride on track 4 is confirmed by measurement, not inference — see
[07-scene-format.md](07-scene-format.md) §7.2.

## 6.5 How much changed between July and October 1995

A great deal. Three concrete findings:

**1. Data types went from 13 to 8.** The July source defines thirteen
(`BOOT LOGICS MODELS ANIMS SCENES SOUNDS SETS WAVES BITMAPS PICTURES SHEETS
CINEPAKS CODES`); the retail disc has eight data tracks. Categories that were
separate — most likely `SCENES` and `PICTURES` — were merged.

**2. The films nearly doubled: 20 in July, 36 on the retail disc.** All `cvid`
320x240. The first one (`BUZZ`, block offset 0) stayed put; every other one
moved. Gaps between consecutive films:

```
July    2577 1407 5593 2152 4141 24784 6634 990 3030 1725 ...   (19 gaps)
retail   544  977 2688 1472  940  5734 4428 4115 2343 15165 ... (35 gaps)
```

**3. There is no longer a Red Book track for speech.** The July source addresses
88 spoken lines (`BO_CDAUDIO_LINE005` .. `LINE100`, up to block 26,653) on a
dedicated audio track. On the retail disc the only audio track is the first one,
10,472 blocks long (2:19) — far too short. The speech went **into the films**:
every Cinepak chunk carries a `STAB` sample table alongside its video, and the
sampled-audio tracks together hold only about 98 seconds, which is a
sound-effect budget. See [09-text-and-fmv.md](09-text-and-fmv.md) §9.2.

**Operational conclusion:** use the July source as an **engine specification**,
not as a disc map. The `DATA.INC` and `CDLINK.INC` tables remain valuable for
the *names* and for knowing what exists, but the offsets have to be
rediscovered from the disc.

What *does* still hold: blocks are 2352 bytes, `GetTrack` works exactly as
documented, the Cinepak FILM format matches `CINEPAK.INC`, and track 7's tag is
literally the `sync_header equ '1111'` declared in the source.

## 6.6 Using the tool

```
python tools/jcd/jcdinfo.py <image.jcd>
python tools/jcd/jcdinfo.py <image.jcd> --extract assets/tracks
python tools/jcd/jcdinfo.py <image.jcd> --hex 7 0 256
```

`--extract` writes one file per track, already de-swapped and with the header
stripped, so offset 0 of each file corresponds to block offset 0 of that data
type.
