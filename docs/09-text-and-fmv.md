# 09 — Localised text and the Cinepak films

---

## 9.1 The text block

The retail build ships **English, French and German** in one block of the
resident binary, at address `$19760`, 6,603 bytes:

* `~` separates records,
* `|` is a line break inside a record.

The block is three consecutive lists — 64 English records, then 65 French, then
64 German. The French list is one longer because one string carries a stray `~`
where a `|` was meant: `UN CLE TROUVEE DANS LES~CABANES DES CHASSEURS` splits
into two records. It does not corrupt the game, because strings are not indexed
positionally.

### Pointer triples

Each item is addressed by **three consecutive longs — English, French, German** —
and those triples sit in the data area next to the code that uses them rather
than in one table. Sixty of them are recoverable by scanning for three longs in
a row that all point into the block in language order:

```
$00B164 -> $019760 "BARE HANDS"   $019F3F "MAINS NUES"   $01A83A "MIT BLOSSEN HÄNDEN"
$00B174 -> $01976B "YOUR FISTS - A BARELY|ADEQUATE WEAPON"  ...
```

Four records have no triple — `KEY`, `BRASS MOGONDAN KEY.|FOR MOGONDAN DOORS?`,
`MAP`, `HUNTER'S ORDERS`. They are presumably reached by an index computed at
runtime; the positional dump gets them.

### The accent encoding

Accented capitals are stored as **lowercase letters**, because the game font
puts them where the lowercase glyphs would be. Three are attested in the shipped
text:

| code | glyph | evidence |
|---|---|---|
| `b` | Ä | `HbNDEN` = HÄNDEN, `JbGER` = JÄGER |
| `d` | Ö | `HdCHSTER` = HÖCHSTER, `dFFNEN` = ÖFFNEN |
| `f` | Ü | `FfR` = FÜR, `SCHLfSSEL` = SCHLÜSSEL |

No other lowercase code occurs anywhere in the block, and the **French text
carries no accents at all** — `CLE`, `EPEE`, `TROUVEE` are all written bare.
Any port should reproduce that or fix it deliberately.

Other string areas in the binary, not yet broken out: `$B167` (menu, German
`IN GEBRAUCH | OPTION BEENDET`), `$24C4B`, `$24F72`, and `$26FA0`, which holds
`HIGHLANDER ONE`, `SETTINGS`, `GAME 1` and the disc-error messages.

```
python tools/text/textx.py TRACK2 --format tsv     # the pointer triples
python tools/text/textx.py TRACK2 --all            # every record, positionally
```

---

## 9.2 The films

Track 7 holds **36 Cinepak films**, all `cvid` 320x240, filling 100,100 of the
track's 102,127 blocks — 98% of it.

They are packed back to back and are **not block aligned**. That is fine because
the player seeks to a block and then scans forward for the sync tag: the code at
`$86C6` looks for the long `'1111'`, which is the track's own content tag and
the `sync_header equ '1111'` of `CINEPAK.S`.

Each film has **64 bytes of `'1111'` immediately in front of its header**, and
behind its last byte there are exactly **56 blocks of zero fill** — 131,712
bytes, on all 35 gaps — before the next film's pad. So the seek is: land on the
block, walk forward to the sync, walk to the end of the run of `'1'`, and the
`FILM` header starts there. Film 0 is the exception that shows the rule: it sits
at byte 0 of the track with no pad in front of it. The zero fill is the slack
the player's read-ahead is allowed to run into.

```
'FILM'  size  0  0                           size is the whole header
'FDSC'  20    'cvid'  height  width          always 240, 320
'CTAB'  size  rate  count   then 16 per chunk:  offset, size, timestamp, tag
```

`FILM`'s size is the length of the header, and each block inside it carries its
own, so the header is walked rather than read at fixed offsets. `CTAB`'s two
spare longs are its tick rate and its chunk count, and `size` is `16 + 16 *
count` on every film on the disc.

Chunk offsets are relative to the film start; the tag is a 4-byte value that
increments per chunk (`$20202020`, `$21212121`, ...). Every chunk opens with its
own 64-byte sync pad — the tag repeated 16 times, the same convention the CD
tracks themselves use — followed by:

```
'STAB'  size  rate  count   then 16 per sample: offset, size, timestamp, type
```

So **the films carry interleaved audio**, and that answers where the speech
went. The July build addressed 88 spoken lines as Red Book audio on a dedicated
track (`BO_CDAUDIO_LINE005` .. `LINE100`); the retail disc has no such track. The
sampled-audio tracks cannot be hiding it either — 78 `WAVE` bundles across
tracks 5 and 6, 98.7 seconds in total, which is a sound-effect budget, not a
dialogue budget. The films hold **18 minutes 23 seconds**.

The boot sequence plays **film 34**, at block 94,340 (`$17084`, the constant
written to the film-offset variable `$44BC` at `$5212`). It is one of the two
largest on the disc: 92 chunks, 15.2 MB.

```
python tools/cinepak/filmls.py TRACK7 --tsv
python tools/cinepak/filmls.py TRACK7 --chunks 34
```

## 9.3 The film audio

An entry in a chunk's `STAB` is sixteen bytes — offset, size, timestamp and a
fourth long — and it is the **timestamp** that says which kind of sample it is:

| | timestamp | offset means | fourth long |
|---|---|---|---|
| **audio** | all ones | where it **ends** — the block is `[offset - size, offset)` | 1 |
| **video** | the film's own ticks, bit 31 a flag | where it **starts** | how long the frame is held |

Both are measured from the first byte after the sample table. Every audio block
on the disc is 16,696 bytes, and the audio is **signed 8-bit mono PCM**.

**The fourth long is a duration, not a type.** It reads as a type for as long
as you only look at the films with a tick rate of 600, where every frame lasts
50 ticks and every video sample therefore says `$32`; it is films 5 and 6 that
give it away, because theirs say 2, and 2 and 3 alternately. Across the whole
track it takes exactly three values — 50, 2 and 3 — plus one short last frame
in each of films 7, 17 and 30, and in every case `duration / rate` is 12 fps.
That is also what makes it safe: a duration of 1 means audio nowhere, and film
17 has one **video** frame of 22,220 bytes whose duration happens to be 1.
`filmwav.py` used to test the fourth long and spliced that frame into film 17's
speech; it now tests the timestamp, and the film comes out 22,220 bytes shorter.

The rate is `audio_in` in `CINEPAK.INC`, **22,252 Hz**, and the disc bears that
out. A one-second chunk normally carries one audio block and every third carries
two, so the long-run rate is `16,696 * 4/3` = 22,261 bytes a second; measured
over the longest films — 173 and 100 chunks — the figure comes to 21,900 and
22,250. Short films read low because the two blocks of prefetch at the head and
the missing block on the tail chunk weigh more.

`CTAB`'s `rate` field is the film's tick rate, and there are three of them:
**600** on 29 films, **30** on six (6, 13, 14, 16, 17 and 26) and **24** on one
(film 5). Frames land 50 ticks apart at rate 600, 2 apart at 24, and 2 and 3
apart alternately at 30 — **12 fps** in all three cases, and the player derives
its wait from the film's own clock rather than assuming the number.

**And bit 31 of a video timestamp is a resume flag.** It is clear on exactly
the frames a player could start at: a frame that carries a whole picture, or
the first frame after an audio block. That is 2,931 of the 13,922 frames on the
disc, and the rule holds on every one of them without exception — which could
only be checked once there was a decoder, since half of the rule is a fact
about the picture and not about the container.

**The reassembly is verified, not assumed.** Concatenating film 9's 227 audio
blocks in `STAB` order gives a signal whose mean absolute sample-to-sample step
across the block joins is 2.55, against 2.50 inside the blocks — the joins are
invisible. Shuffling the same blocks raises it to 6.93.

```
python tools/cinepak/filmwav.py TRACK7 --out assets/filmaudio
```

35 of the 36 films carry sound; film 16, five chunks long, is silent.

## 9.4 Which film is which

The 20 names in July's `DATA.INC` are alphabetical and the block offsets all
moved, so the names cannot be mapped by position. The route through the data
works: session 5 disassembled the script VM, and every `cinepak` command names a
film by block offset. Together with the three `CINEPAK` events and the boot
film, that places **33 of the 36** — see [11-script-vm.md](11-script-vm.md)
§11.7 for the table and for the four that are named by what the code does with
them. Films 15, 28 and 33 still have no trigger anywhere on the disc.

---

## 9.5 The decoder

The container had to be worked out; the codec did not.
[05-roadmap.md](05-roadmap.md) §5.2 puts the hand-written `CINEPAK.S` on the
list of things to read rather than transcribe — it is a Jaguar decoder, all
blitter and GPU, and what it decodes is standard `cvid`. So the decoder here is
written from the published format and then checked against the disc.

```
src/media/film.c      the container: seek to a block, find the sync, walk the
                      chunks, hand out video frames and speech
src/media/cinepak.c   the cvid decoder, into 24-bit and then into RGB16
tools/cinepak/filmdec.py   the same codec again, in Python, as a second opinion
```

A frame is ten bytes and then strips — horizontal bands, two of 320x120 on this
disc — and each strip carries its own pair of codebooks and the vectors that
index them:

```
flags(1) length(3) width(2) height(2) strips(2)
strip:  id(2) size(2) y0(2) x0(2) height(2) width(2)
  $20 / $21   V4 codebook, whole / updated under a flag word
  $22 / $23   V1 codebook, likewise
  $30         vectors: one flag bit per block, V1 or V4
  $31         vectors: a bit for "coded at all", then the V1/V4 bit
  $32         vectors: V1 throughout
```

A codebook entry is four Y and a signed U and V and paints a 2x2 block: **V1**
doubles one entry over a whole 4x4, **V4** puts four entries in its four
quadrants. An inter frame simply leaves the blocks it does not code alone. The
colour is Cinepak's own, `R = Y + 2V`, `G = Y - U/2 - V`, `B = Y + 2U`, with the
halving an arithmetic shift; the Jaguar's own decoder went straight to RGB16 and
may well round differently, which is a question for a frame comparator against
an emulator and not for this disc.

Two details of the format only showed up because the disc uses them. A strip
that sends no codebook of its own **continues the strip above it** — this
disc's second strip is usually two empty codebook chunks and nothing else — and
a codebook chunk is allowed to **stop early**, leaving the rest of the 256
entries as they were. The first frame of most films is a single V4 entry over
the whole picture, which is to say: black.

### What it is checked against

```
build/hlview --check-film
```

decodes **every frame of all 36 films** — 13,922 of them, 1,631 carrying a
whole picture — and three counts come out at zero:

* **no decoder error.** A Cinepak stream read a byte out of step runs out of
  vectors before it runs out of picture, so drift shows up as an error here
  rather than as a smear on the screen.
* **no chunk whose sample table fails to account for its bytes.** The sync pad
  plus the `STAB` plus the samples it lists comes to the chunk's size exactly,
  on all 1,175 of them.
* **no frame whose timestamp disagrees with its picture** — the resume flag
  above, which needs the decoder to check.

Every film comes out at 12.0 fps: 1,160 seconds of picture, against 1,102
seconds of speech (film 16 is silent, and the last chunk of a film carries no
audio block).

Two independent readings agree, which is the point of writing the second one:

* the 36 block offsets `film_scan` walks are the 36 in `assets/films.tsv`,
  which `filmls.py` wrote months earlier;
* the chunk, frame and audio-block counts per film are identical to
  `python tools/cinepak/filmdec.py TRACK7 --frames`;
* and a frame decoded twice is **byte for byte the same file** —
  `hlview --film 19 --shot-at 30 --shot c.ppm --no-window` against
  `filmdec.py --film 19 --frame 30 --ppm f.ppm`, 230,415 bytes of PPM. Frame
  400 of film 6 comes out identical too, which is the better of the two: it is
  401 frames deep into the chain of inter frames, on a film whose tick rate is
  30, so a decoder that drifted anywhere in those 400 frames would not land on
  the same picture.

### Playing one

```
build/hlview --film 19                     at its own 12 fps, in a 320x240 window
build/hlview --film 9 --scale 2            the intro
build/hlview --film 19 --shot-at 30 --shot f.ppm --no-window
```

and inside the game loop it needs no more than the handshake the script machine
already uses ([11-script-vm.md](11-script-vm.md) 11.9): the machine posts
`EVENT_TYPE_CINEPAK + 1` and runs no further command until it is cleared, so
the host plays the film and clears the event.

```
build/hlview --scene SHANR1_CAM00 --char 0 --drive

frame    3: a script asks for the film at block 49055
the film at block 49055: cvid 320x240, 8 chunks, 210 ticks at a rate of 30
91 frames, 14 of them whole pictures, played in 7.5 s at 12.1 fps
frame    4: (5, 649) triangle 34 floor 2638
```

The window changes shape for the film and back again afterwards, which is what
the original did too — in video mode rather than in SDL.

The films are readable now, and that is worth saying on its own: film 5 turns
out to carry the credits in plain text — *3D Engine, Andrew Harris, Magenta
Software Ltd; Character Model Artist, Paul Johnson; Project Manager, Andrew
Rickard; Project Coordinator, Mark A. Cargill* — and film 34 opens on the Atari
logo over the standing stones. §9.4 named the films by what the code does with
them; they can now also be named by looking at them, which is what the three
with no trigger anywhere on the disc need.
