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
| 2 | — | `0x20` | 120 | 0.3 | boot, 68000 code |
| 3 | `DATA` | `0x21` | 2,689 | 6.0 | to be identified |
| 4 | `PICT` | `0x22` | 73,921 | 165.8 | **scenes**: backdrops plus Z-buffer |
| 5 | `DATA` | `0x23` | 1,849 | 4.1 | to be identified |
| 6 | `DATA` | `0x24` | 2,129 | 4.8 | audio samples (`WAVE` marker at +4) |
| 7 | `1111` | `0x25` | 102,127 | 229.1 | **Cinepak** (the tag is the `sync_header` from `CINEPAK.INC`) |
| 8 | `DATA` | `0x26` | 561 | 1.3 | to be identified |
| 9 | `DATA` | `0x27` | 67 | 0.2 | to be identified, high-entropy content |

193,935 blocks total, about 456 MB on a ~700 MB disc.

Working hypothesis for track 4: 73,921 / 110 = 672.009, so roughly **672 scenes
of 110 blocks each** (the July build had 594). To be confirmed visually.

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
10,472 blocks long (2:19) — far too short. The speech moved somewhere else, most
likely into the films or among the samples.

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
