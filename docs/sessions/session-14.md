# Session 14 — The word that stopped moving, the sixteenth piece, and the Jaguar rounds

Four sessions running, the TODO has opened with **the frame comparator**, and
four times it has been skipped as "blocked on an emulator". It is not blocked,
and this session it ran. A frame of the boot film was taken out of the
Jaguar's own memory under BigPEmu and set against the same frame out of this
decoder, and 9.5's four-session-old question — does the hardware round where we
truncate — has a number instead of a shrug.

Two of the other items closed outright, and one of them turned out to have a
cause nobody had guessed.

→ [09-text-and-fmv.md](../09-text-and-fmv.md) 9.5,
[15-combat.md](../15-combat.md) 15.6 and 15.8,
[16-inventory.md](../16-inventory.md) 16.9

---

## 1. The face-to-face stand-off was not the AI's pause frames

Session 13 found the bug and wrote down a diagnosis: two characters squared up
never land a blow because `AIAttackCode` cycles attack, defend and pause, and
on the pause frames it drops the buttons, so `ActionCode` picks the stand row
and restarts the swing. The recommendation was to read `ActionCode` properly.

`ActionCode` read properly says the diagnosis is half right and the conclusion
is wrong. It does restart on a change of row — the comparison is `actAction`
against the *masked* joypad, so a change of logic-table row and nothing else —
and the pause frames really do drop the buttons. But the pause frames are
**meant** to be there. A real fight is runs of attack frames separated by
pauses, and a swing lands when a run happens to be long enough.

What was wrong is that in this port there was a pause frame after *every*
attack frame. A trace of `--drive --fight` shows it with no interpretation
needed:

```
T    7 pad 00000060 act 00000040 anim 21 st2 d 499
T    8 pad 00000000 act 00000000 anim  6 st0 d 499
T    9 pad 00000060 act 00000040 anim 21 st2 d 499
T   10 pad 00000000 act 00000000 anim  6 st0 d 499
...for ever, and the distance never changes either
```

`actStatus` alternating 0, 2, 0, 2 on a two-frame period, and the animation
frame never leaving zero.

### `AIRandomCode`'s word belongs to the frame

The cause is one line of `AICTRL.GAS` that this port had read one word short:

```
ControlCode:
	movei	#framecount,reg1		; get frame count
	load	(reg1),reg1
	...
.loop:					; and only now the characters
```

`framecount` goes into reg1 **before** the loop over the active characters.
`AIRandomCode` — which every `btst` in `AIAttackCode` reads — then overwrites
reg1 with that word squared, mixed with both characters' coordinates and rolled
a byte at a time, and leaves it there for whoever is processed next. So there
is **one running word per frame**, re-seeded from the frame counter every frame
and shared down the table within it.

[15-combat.md](../15-combat.md) 15.6 had this right in prose since session 12.
The port had it as a per-character `seed`, set to 1 at birth and fed nothing
afterwards but its own output.

Deprived of the frame count, that word is a function of the four coordinates
alone. Two people squared up and standing still hand it the same four for
ever — and it settles into a two-frame cycle. Attack, pause, attack, pause,
with `ActionCode` throwing the swing away one frame after it starts.

The fix is the original's arrangement: one `uint32_t` in `act_frame`, seeded
with the frame count, handed to every AI character in the pass through
`AiWorld.rnd`.

### And the check measures the failure rather than asserting its absence

The first attempt at a regression test was a geometric one — put a pair face to
face, wait for a blow — and it did not discriminate: the old code lands blows
in eight placements out of eight, because in those placements the two of them
*close*, so the coordinates never freeze. It is the frozen case that fails, and
staging it reliably was harder than testing the mechanism.

So `--check-combat` grew a fourth part that runs the attack machine on its own,
with the pair frozen, for six hundred frames — **twice**: once feeding the
running word its own output, and once reloading it with the frame count.

```
the running word: frozen face to face, the longest run of consecutive attack
  frames is 1 carried from frame to frame and 3 reloaded with the frame count
```

The first number has to be 1 or the check is not looking at the failure at all.
The second has to be larger or the fix has stopped working. Both halves are in
the check, so it cannot pass for the wrong reason.

### What it changes

```
duels: 11 fought one on one, 11 ended with somebody dead, 277 frames each
                                                          (617 before)
```

and, in the game rather than the harness, `--drive --fight` with nothing held
on the pad now ends with

```
frame   22: player is hit, life 235
```

which the hunter had never once managed.

## 2. The sixteenth piece

`EVENT.GAS` had already told session 13 what to do — "immediately change model
16 of player character to object specified" — and left two questions: where
does the sixteenth piece hang, and what turns it. Both are answered in the
data and in one more routine.

**Where.** Quentin's right hand — piece 6, whose own origin is 140 — publishes
exactly one origin point, number **144**, and it is the only origin on the disc
above 141. 14.1 had drawn it into the skeleton months ago and called it "what
the hand hands on to, the weapon".

**What turns it.** `ANIM.GAS` carries a routine called `swordcode`, "new code
to track down and fix up the rotation data for the sword". It walks two
pointers down the draw list nine entries apart until the leading one finds the
entry with status bit 12 set, and copies three rotation words from the trailing
one into it. Nine back from the sixteenth is the seventh, which is piece 6,
which is the right hand.

It could not work any other way. Orientations do not chain in this engine —
every piece's angles are absolute and come from the animation — and every one
of the 285 animation records on track 5 carries **fifteen** sets of angles. A
sixteenth piece has nowhere to get an orientation except by copying one.

So `actor_inhand` puts the held object at origin 144, carried through the
hand's rotation the way every other joint is carried, wearing the hand's own
angles. `--drive --weapon 2` now draws the sword, and taking `--weapon` off
takes it away again: the same 56 pixels and no others.

`--check-inventory` opens with the claim stated as a question to the data:

```
the sixteenth piece: 14 bundles with a skeleton, 1 publish origin 144 exactly
  once, 1 of those from piece 6, whose own origin is 140; 0 other origins
  above 141
```

**One** of the fourteen skeletons publishes 144, and that is not an omission:
it is `changetomod`'s own first line, *model 16 of **player** character*.
Nobody else on the disc is ever drawn holding anything.

## 3. The comparator, and the Jaguar rounds

`tools/emu/` is new and is the comparator's two ends.

The Jaguar end is a **BigPEmu script module**, `hl_cine.c`, compiled by the
emulator itself. The first version read the film player's framebuffer at the
address `CINEPAK.INC` fixes — `screen_start $000C0000`, `'FILM'` at
`film_start $0010E000` — and that turned out to be wrong on the retail disc:
with the boot film visibly on screen, `$0010E000` holds `12441245`, which is
two RGB16 pixels. The source dump is a July 1995 build and the release moved
its memory. So the probe stopped guessing and takes **the whole 2 MB** every
four seconds instead. Thirty dumps cover the boot film twice over.

The PC end finds the picture in that without being told where it is. Three
scores were tried before one worked, and the two failures are worth as much as
the answer:

* **the row difference alone** finds a black hole. Empty memory agrees with
  itself perfectly, and 2 MB of Jaguar RAM has a great deal of empty in it.
* **the row difference over the window's spread** finds the flattest window the
  spread floor allows, wherever the floor is put. The optimum was always "as
  smooth as permitted", which is a gradient, not a picture.
* **the row difference over the difference at a deliberately wrong stride**
  works, because it asks the one question a framebuffer answers differently
  from anything else: do the rows line up at *exactly* this stride and not at
  one eight pixels off. A photograph stops matching when it is shifted
  sideways; a fill and a ramp do not care.

That detector is checked the way a detector that is told nothing has to be:
`fakeram.py` plants one of *our* decoded frames in 2 MB of noise, in either
layout, and `findfb.py` has to come back with the address it was planted at.
It does, in both, exactly.

### Two things about the framebuffer, and both confirm the 1995 source

The film player's screen really is **two buffers interleaved a phrase at a
time** — `CINEPAK.INC`'s `screen_gap 2` — and the two hold the same picture
except where the newest frame has changed something. And the pixel is
**R5 B5 G6**, which `COLLECT.GAS` writes down in its comment on
`darken_screen` as "RBG = 5:5:6".

`src/media/cinepak.c` had that right all along. The comparator did not, for one
evening, and it was caught the only way it could be: our decode of the frame
came out a blue-violet vortex and the Jaguar's came out green. Read with green
in the middle, blue *is* green. A comparator is a piece of code too.

### The answer

```
                        pixels identical    mean error R / G / B
  ours truncated             17.4%          -0.519  -0.438  -0.487
  ours rounded to nearest    53.7%          -0.007  -0.018  -0.021
```

Half a step low on all three channels, and a mean of zero to three decimals
when it is rounded. On another dump — frame 721 rather than 529 — it is 19.3%
against **84.2%**, and on frame 193, 11.6% against **91.8%**. Six frames were
compared and rounding wins on all six.

`cinepak_rgb16` rounds now, and `cmpfb.py` prints both reductions on purpose:
the finding is not "rounding looks better", it is "truncating puts every
channel half a step low", and a tool that showed only the winner would not be
showing that.

What is left is second order and is written down rather than hidden. Rounded,
**79.7% of red values are exactly right and 98.3% are within one step**; green
and blue only 65–69% exactly. Red is the one channel Cinepak computes without
a halving — `R = Y + 2V` against `G = Y - U/2 - V` — so the residue is where
this decoder's *arithmetic* differs from the Jaguar's rather than its
rounding: ours clamps to eight bits per channel and then reduces, and the
hardware had no eight-bit step to clamp at.

### Getting the emulator to run at all

Two things, both settled by experiment rather than by reading:

* **BigPEmu does not load the `.jcd`.** Four boots — cue and jcd, with and
  without the cartridge boot ROM — and only the cue/bin set ever gets the 68000
  running in RAM. The probe's own heartbeat is what says so, rather than what
  the window looked like.
* **A Jaguar CD needs a CD BIOS**, which is a setting of its own and is not the
  cartridge BIOS. The emulator's own strings say it outright:
  *"Audio discs will not be loaded unless a CD BIOS is provided"* — and every
  track of a Jaguar CD cue is marked `TRACK AUDIO`.

## What is checked

Eight checks, all passing, two of them with new parts:

```
build/hlview --check-combat      four parts now: the animation roles, eleven
                                 duels, the two hunters, and the running word
build/hlview --check-inventory   opens with the sixteenth piece
```

and `--check-char`, `--check-doors`, `--check-film`, `--check-follow`,
`--check-mesh`, `--check-script` unchanged.

## Still open

* **The last 20% of the picture.** Rounded, red matches the hardware on 79.7%
  of pixels exactly and 98.3% within one step; green and blue on 65–69% and
  89–92%. The difference is arithmetic rather than rounding — this decoder
  clamps to eight bits per channel before reducing and the Jaguar had no
  eight-bit step — and closing it means carrying Y, U and V to the reduction
  instead of RGB.
* **A projectile**, unchanged: bank 3's 1,500-unit reach is applied as an
  ordinary blow.
* **The film audio, phase 7**, carried for the fourth session.
* **The logic tables.** What writes `misc[0]` is still unfound.
* **The 15th AI command**; **the low byte of `cshBehaviour`**; **the combat
  sounds**.
* Unchanged: track 9 and `HIRESDATA`; `ZMODELT`'s sense; the 125 unnamed world
  records; the seventeen unnamed GPU modules; the `SLP` payload; `gvar[0..2]`;
  films 15, 28 and 33; world records 143 and 144.

## TODO for session 15

### 1. Point the probe at the game rather than at the film

The comparator worked, and everything it needed is now built and checked. The
next question it can answer is the bigger one: the boot film runs the Cinepak
player, but the *game* runs `ActionCode` and `PPCOLL`, and those are the parts
this port has been guessing at.

`hl_cine.c` dumps memory blind, which was right when nothing was known. A probe
for the game wants to read *structures* — `activchar`, the CIT, the world table
— and for that the addresses have to be found first, the same way the
framebuffer was: not from `CINEPAK.INC`, which the release moved, but by
searching the dump for something whose shape is known. The world table is 197
records of a known layout and is the obvious anchor.

That settles **§15.2's knockback codes** — which of the four reactions the
68000 actually picks is a `citAnimate` read once the CIT is found — and the
**combat timing**, and it is how the logic tables could finally be located,
since `misc[0]` points at one.

### 2. The film audio, phase 7

Fourth session carried. `film.c` hands out every audio block in order, signed
8-bit mono at 22,252 Hz, and the films run on their own timestamps, so the
video is the clock. The combat sounds want the same mixer, and so do the four
`sound*` entries on every character sheet.

### 3. The last 20% of the picture

Rounding closed most of the gap and the rest is arithmetic: this decoder goes
Y,U,V → eight-bit RGB → five and six bits, and the clamp in the middle costs
precision the Jaguar never spent. Carrying the full-precision values to the
reduction is a small change to `cinepak.c` and `filmdec.py` both, and
`cmpfb.py` already measures whether it helps — red at 98.3% within one step is
the number to beat.

### Smaller

* **The films, named by looking at them** — 15, 28 and 33.
* **World records 143 and 144**, whose `wstSheet` points at nothing.
* **The 15th AI command**, which `LOGICS.INC` does not number.
