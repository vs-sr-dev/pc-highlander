# Session 11 — The films run

Session 10's TODO put the films first, and said the hole was exactly
film-shaped: the container read, the audio verified, 32 of the 36 placed by
block offset, `game.want_film` already carrying what a script asked for and the
loop already throwing it away. What was missing was one decoder.

It is in, and the container turned out to have one thing left to say on the way
past — which is what unblocked six films that would otherwise have decoded to
nothing.

---

## 1. The fourth field of a sample entry is a duration, not a type

§9.3 had a `STAB` entry as *offset, size, timestamp, type*, with type `$32` for
a video frame and `1` for an audio block. That reads every film on the disc
except two, and the two it fails are the ones whose tick rate is not 600:

```
film 5, rate 24     samples of type 2
film 6, rate 30     samples of type 2 and type 3, alternating
```

A decoder that looks for type `$32` finds **no video at all** in either — and
in six films, once the rest of the track is counted, because the rate is 30 on
films 6, 13, 14, 16, 17 and 26 and 24 on film 5. (§9.3 said 600 on 34 of the
36; it is 600 on 29.)

The fourth long is **how long the frame is held**. At a rate of 600 that is 50
ticks, every time, which is why it looked like a constant. At 24 it is 2 and at
30 it is 2 and 3 alternately, and `duration / rate` is **12 fps** in all three
cases — so the player takes its wait from the film's own clock rather than from
a number written into it. The whole track uses three durations and no others,
plus one short last frame in each of films 7, 17 and 30.

What says audio is the **timestamp**, which is all ones and nothing else's is.

**And that is a bug, not just a tidier reading.** Film 17 has a video frame of
22,220 bytes whose duration happens to be 1. `filmwav.py` tested the fourth
long, so it spliced that frame into film 17's speech; it now tests the
timestamp, and film 17 comes out 22,220 bytes shorter. Every other film is
unchanged.

→ [09-text-and-fmv.md](../09-text-and-fmv.md) 9.3

## 2. Bit 31 of a video timestamp is a resume flag

§9.3 said "bit 31 is a flag" and left it there, because the container alone
cannot say more. With a decoder it can: bit 31 is clear on **exactly** the
frames a player could start at — a frame carrying a whole picture, or the first
frame after an audio block.

That is 2,931 of the 13,922 frames on the disc, and it holds on every one of
them. Half the rule is a fact about the container and half is a fact about the
picture, which is why it had to wait for a decoder to check it, and why it is
worth stating: `--check-film` now asserts it on every frame, so the container
reader and the codec check each other.

## 3. The decoder, written rather than transcribed

[05-roadmap.md](../05-roadmap.md) §5.2 lists the hand-written Cinepak decoder
among the things to read and not copy — it is a Jaguar decoder, all blitter and
GPU choreography, and what it decodes is standard `cvid`. So:

```
src/media/film.c      the container: seek to a block, find the '1111' sync,
                      walk the chunks, hand out frames and speech
src/media/cinepak.c   the codec: codebooks, 4x4 blocks, V1 and V4, inter and
                      intra frames, into 24-bit and then into RGB16
```

Two details of the format only showed up because this disc leans on them. A
strip that sends no codebook of its own **continues the strip above it** — this
disc's second strip is usually two empty codebook chunks and nothing else — and
a codebook chunk may **stop early**, leaving the rest of its 256 entries alone.
The first frame of most films is one V4 entry stretched over the whole picture,
which is to say: black.

Two smaller things fell out of the container on the way. Every film has 64
bytes of `'1111'` immediately in front of its header, and behind its last byte
sit exactly **56 blocks of zero fill** — 131,712 bytes, on all 35 gaps — before
the next film's pad. So the seek is: land on the block, walk to the sync, walk
to the end of the run, and the header starts there. Film 0 is the exception
that shows the rule, at byte 0 with no pad in front of it.

→ [09-text-and-fmv.md](../09-text-and-fmv.md) 9.5

## 4. What it is checked against

Not "it looks right". `hlview --check-film` decodes **every frame of all 36
films** and three counts have to come out at zero:

```
films: 36 of them, 13922 frames decoded, 1631 whole pictures
  0 decoder errors, 0 chunks their sample table does not account for
  0 frames whose timestamp disagrees with the picture
  1160 seconds of picture at 12.0 fps, and 24529676 bytes of speech,
  which is 1102 seconds at 22,252 Hz
```

The first matters because a Cinepak stream read a byte out of step runs out of
vectors before it runs out of picture — drift shows up as an error rather than
as a smear. The second is the container's own arithmetic: sync pad plus sample
table plus the samples it lists comes to the chunk's size exactly, on all 1,175
chunks. The third is §2 above.

Then three agreements, each between things written at different times:

* the 36 block offsets the C inventory walks are the 36 in `assets/films.tsv`,
  which `filmls.py` wrote months ago;
* the chunk, frame and audio-block counts per film are identical to what
  `python tools/cinepak/filmdec.py TRACK7 --frames` counts;
* and **a frame decoded twice is byte for byte the same file** — 230,415
  bytes of PPM, out of `hlview --film 19 --shot-at 30` and out of
  `filmdec.py --film 19 --frame 30`, C and Python. Frame 400 of film 6 comes
  out identical as well, which is the better of the two: 401 frames deep into
  the chain of inter frames, on a film whose tick rate is 30, so a decoder that
  drifted anywhere along the way would not land on the same picture.

**What that does and does not settle.** Both decoders are this session's, so
their agreement rules out a transcription slip, not a shared misreading of the
format; and the colour conversion is the published `R = Y + 2V`, `G = Y - U/2 -
V`, `B = Y + 2U`, where the Jaguar's own decoder went straight to RGB16 and may
round differently. Both of those are one frame diff against an emulator away
from being settled, which is the frame comparator that has now been on the list
for four sessions.

## 5. And a script plays one

The integration is the handshake and nothing more, because the handshake is the
original's: the machine posts `EVENT_TYPE_CINEPAK + 1` and runs no further
command until it is cleared.

```
build/hlview --scene SHANR1_CAM00 --char 0 --drive

frame    3: a script asks for the film at block 49055
the film at block 49055: cvid 320x240, 8 chunks, 210 ticks at a rate of 30
91 frames, 14 of them whole pictures, played in 7.5 s at 12.1 fps
frame    4: (5, 649) triangle 34 floor 2638
```

Block 49055 is film 14, one of `SHANR1`'s three in §11.7. The window changes
shape for the film and back again afterwards, which is what the original did
too — in video mode rather than in SDL.

## 6. The films can be read now

Which is worth saying on its own, because §9.4 could only name the films by
what the code does with them. Film 5 carries the credits in plain text — *3D
Engine, Andrew Harris, Magenta Software Ltd; Character Model Artist, Paul
Johnson; Project Manager, Andrew Rickard; Project Coordinator, Mark A.
Cargill* — which confirms by reading it what §11.7 had inferred from the main
menu's third item. Film 34 opens on the Atari logo over the standing stones,
and film 9 on the stones themselves.

The three films with no trigger anywhere on the disc — 15, 28 and 33 — can now
be identified by watching them, and the retail name list can be checked against
July's alphabetical one instead of being argued from consistency.

---

## Still open

* **Combat**, unchanged: `AIAttackCode` is ported down to the point where it
  needs `actStatus`, the hit frames and the opponent's own joypad.
* **The logic tables**, unchanged from session 9: nothing on the retail disc
  fills `misc[0]`, so the table is resident and written by code, and
  `CSHCODE.GAS` is still the file whose name suggests it.
* **The 15th AI command**, which `LOGICS.INC` does not number.
* **The low byte of `cshBehaviour`** on the item and weapon sheets.
* **The film audio**, which is read and handed out but not played: that is
  phase 7, and it now has a clock to lock to.
* Unchanged: track 9 and `HIRESDATA`; `ZMODELT`'s sense; the non-uniform `face`
  elevation on the items; the 125 unnamed world records; the seventeen unnamed
  GPU modules; the `SLP` payload; `gvar[0..2]`.

## TODO for session 12

### 1. Combat, or at least its movement half

Now the biggest thing left in the game itself. `AIAttackCode` is ported down to
the point where it needs `actStatus`, the hit frames and the opponent's own
joypad — which is `PCOL.TXT`, and phase 5. The three sheets with
`aiAttackPlayer` and the one with `aiShootPlayer` are waiting for it, and
`ACT_CREATED` already means a hunter can be put in a set.
**Success criterion:** you can fight a Hunter and one of you dies.

### 2. The frame comparator

Four sessions have wanted it and this session gives it a second job: it is what
turns "two decoders of mine agree" into "the picture is the picture the Jaguar
drew", and it would settle the colour rounding in an afternoon. Render a scene,
run the same scene under an emulator, diff. Session 9 spent a day on a number
one frame diff would have settled.

### 3. The films, named by looking at them

Cheap now, and it closes §9.4. Decode a frame from each of the 36 — `hlview
--film N --shot-at K --shot f.ppm --no-window` — and name them; films 15, 28
and 33 have no trigger anywhere on the disc, and the retail list can be checked
against July's alphabetical twenty rather than argued from consistency. Film 5
already answered for itself.

### 4. The film audio, which is phase 7's

`film.c` hands out every audio block of a chunk in order, signed 8-bit mono at
22,252 Hz, and the reassembly was verified in session 9 by the step size across
the joins. What is missing is a mixer to put it into. The films now run on
their own timestamps, so the video is the clock and the audio is what gets
locked to it — not the other way round.

### 5. The logic tables

Still a code search rather than a disc search: find what writes `misc[0]` of a
character sheet. `CSHCODE.GAS` is the file in the dump whose name suggests it.
Until then both joypad-to-animation tables are ours.

### Smaller, and still worth an hour each

* **The 15th AI command**, which `LOGICS.INC` does not number and one sheet
  uses.
* **The low byte of `cshBehaviour`** — 10, 20, 30, 40 or 250, on the item and
  weapon sheets only.
