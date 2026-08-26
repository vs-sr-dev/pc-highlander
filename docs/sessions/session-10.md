# Session 10 — Ramirez, and the machine that watches him

Session 9's TODO put Ramirez first: "the init table has been carrying his
arrival point in bit 0 of every doorway all along, `AICTRL.GAS` has an
`aiFollowPlayer` command, and `cshBehaviour` is the selector 12.8 has been
waiting to identify. A second character who follows is the next thing that
makes it a game."

He is in, and each of those three turned out to be a whole answer rather than a
lead. Nothing had to be invented: the sheet says who he is, one byte of it says
what he does, and the doorway table has been saying where he stands since
session 9 read it.

---

## 1. `cshBehaviour` is two bytes, and the high one is an AI command

§12.8 had it as "eleven distinct values — 0, 10, 20, 30, 40, 250, 1536, 1792,
2304, 2816, 3840 — presumably an AI selector". Read as two bytes instead of one
word the list stops being eleven arbitrary numbers:

| sheets | high byte | low byte |
|---|---|---|
| Quentin | 0 `aiNop` | 0 |
| the hunters | 6 `aiAttackPlayer` | 0 |
| the one with a gun | 11 `aiShootPlayer` | 0 |
| four in a row | 9 `aiFollowPlayer` | 0 |
| the shopkeepers | 7 `aiFacePlayer` | 0 |
| the weapons and items | 0 | 10, 20, 30, 40 or 250 |

The high byte is `ControlCodeTable`'s index and nothing else. **The check is
that neither end is a number you would land on by accident**: the player reads
`aiNop`, because the pad drives him, and exactly one sheet reads
`aiShootPlayer` — the one whose 1995 counterpart is `HUNTD`, the hunter with
the gun.

The low byte is non-zero **only** on the twenty-one sheets whose high byte is
zero, so it is an item property sharing the word. What it counts is not known:
it is not the model, the world flags, or any of the four stat bytes.

One sheet reads 15, which is past the fourteen commands `LOGICS.INC` numbers.
Five world records use it, and its 1995 counterpart is `FAVED`, the claw. So
retail added at least one command; which one is open.

## 2. Which sheet is Ramirez, and which bundle is his

`WORLD.INC` says `WORLD_RAMIREZ equ 1`, and retail's world record 1 is July's
line for him byte for byte — sanity 242, personality 6, strength 229, life 229,
`WSTDeactivated` — with `wstSheet` zero, because a script hands him one.

Shape and order settle the rest together. The chain is in `SHEET.S`'s order,
and four sheets in a row carry 15 models, 4 animations and `aiFollowPlayer`,
exactly where `SHEET.S` has `RAMIREZ`, `FAVEB`, `MANGUA` and `ARAKA`. The first
of the four is sheet 7, and its one file record names track 5 block 1176 —
`1176 * 2352 + 4`, which is where **bundle 9** begins.

That is not a coincidence to be taken on trust, because the same rule resolves
the whole chain: **sheets 1 to 16 land on bundles 0 to 15, none shared, none
left over.** `hlview --list-sheets` prints it.

His four animations, read off their own root motion the way Quentin's thirty
were, are `BO_LOGICS_2A` — "stand & walk" — item for item:

```
 0    5 frames, nothing moves                  stand
 1   25 frames, +381 along z, 15.2 a frame     walk
 2   15 frames, +51 of facing, 3.4 a frame     turn left
 3   15 frames, -56 of facing, 3.7 a frame     turn right
```

## 3. He presses buttons

`AICTRL.GAS` runs **two** master loops over the active characters, and the
difference between them is the whole design. `ControlCode` fills in `actJoypad`
for each — through `PlayerControl` for the one the pad is driving, through
`ComputerControl` for everyone else. `ActionCode` then turns that joypad into
an animation, and it cannot tell which loop wrote it.

So there is no AI character anywhere below `src/game/ai.c`. He is an `Actor`
with a `Control`, stepped by the same `control_action`, the same
`control_turn`, the same `actor_step`. The only line that differs is which
function produced the pad.

`ComputerControl` is one jump through `ControlCodeTable`. Each of the fourteen
commands is three pieces — where the target is, `AIRotateCode`, and one of
face / goto / follow / attack — and `AIFollowCode` is four lines:

```
turn towards the player
if dx*dx + dz*dz > 625*625      follow_range = 1000/4*5/2, two and a half metres
    press forward
```

There is no path finding in it, and none anywhere else in the original either.

**`AIRotateCode` needed no transcribing.** It indexes a 257-byte `arctan_table`
in `JOY.S`, and every one of those 257 entries is exactly
`round(atan(i / 256) * 512 / pi)` — so `ai.c` builds the table, and that
equality is the reason it is allowed to.

## 4. What the disc had ready for him

Session 9 read the init table's flags word as `facing * 256 + character`, bit 0
being 0 for the player and 1 for the companion, with 44 ids carrying both
halves. That bit now does its job. On a doorway the player is put down from his
entry and the companion from the one beside it, keyed on the same view left:

```
cut to C3_CAM19 (id 211) at (2220,26556) on triangle 0
  through the door into set 14: arriving at (-3192,6358) facing 128, from view 1037
  the companion arrives at (-2265,6194) facing 128
```

`hlview --check-follow` runs the whole of it over the disc rather than over one
walk. Twenty sets carry an entry pair; in each, both are put down where the
table says, the player walks forward for 300 frames and then stands.

* **The bearing is right everywhere.** Over 3,600 directions the worst
  disagreement with the arctangent is one step of 256 — the table's own
  quarter-step resolution, rounded. The three sign tests are where a port of
  `AIRotateCode` goes wrong, and a swapped quadrant would have shown as 64.
* **He never leaves the mesh** — 0 frames off it in all twenty sets. That is
  the invariant that matters: the ground height is indexed by the triangle he
  stands on.
* **Sixteen of the twenty close back up** to inside 625 units. The other four
  are stopped by geometry, which is not a bug to fix: `AIFollowCode` walks
  straight at you and always did.

## 5. What phase 4 was for

```
build/hlview --scene DUN1_CAM00 --char 0 --drive
```

Quentin walks around `DUN1` under the pad, the camera cuts where the event
lines say, the doors lead into the next set — and Ramirez comes too. That is
phase 4's stated success criterion, and a little more than it.

## 6. And the scripts, running

Session 9's second item, in the same evening. `src/script/vm.c` is
`SCRIPT.GAS`'s interpreter written out: all 83 opcodes, the process table with
its compacted list and its identifiers, the five condition masks, and the
world-state and instance access the commands reach through.

**It needed a table underneath it first.** Almost every command that does
something to the world does it to an active character table record, so
`src/game/act.c` is `LOGICS.INC`'s `actRecord` — and once that exists,
`AICTRL.GAS`'s two master loops belong to it rather than to `main.c`, and the
script commands get short. `chase` is four lines: it writes `aiGotoPerson` into
a record the AI loop is already walking. `freeze` is `aiNop`. `release` clears
one bit — `ACTControlled`, which is deliberately not "is he the player", so a
script takes the pad away from the player and hands it back with the same flag.

**The check runs every script on the disc.** Each of the twenty-seven set
scripts is loaded beside the resident one and run for 1,200 game frames against
a real set and a real character — one pass as you would walk in, one with the
world stirred underneath it, and MAINSCRIPT's orphan "item used" block started
the way the engine starts it, from outside the bytecode.

```
scripts: 48 sets, 27 of them with one, plus MAINSCRIPT in every run
  585,226 commands executed, 46 of the 83 opcodes exercised
  0 past the dispatch table, 0 processes overran, 0 fields with no home
  2 processes ran into their slot's padding and stopped
  10 camera cuts asked for, 0 of them to a view the set does not list, 22 films
```

Nothing past the dispatch table is the one that matters: the original abandons
a process on a bad command, so a decoder that drifts by a byte shows up as
scripts quietly dying rather than as an error. Nothing overran, so every loop
on the disc reaches a `quit`. And every `camera` named a view its own set
lists — an operand checked against a table the VM never reads.

**One divergence, stated.** A zero command is slot padding here and `not r0` on
the real machine, which would execute it for ever. Two sets do exactly that:
set 4's `ScriptOffset` points straight at padding and set 37's script is two
commands and then padding. The disassembler measured those two at 0 and 8 bytes
independently, and no shipped script uses `not` at all.

The machine is not wired into the live loop yet — that wants the game loop
lifted out of `main.c`, which is the next thing rather than a late-evening one.

→ [11-script-vm.md](../11-script-vm.md) 11.9

## 7. And the loop is a loop

`--drive` was a branch inside a viewer, and the machine had nowhere to run.
`src/game/game.c` is one game frame, in the order the original runs it:

```
1  the script machine, one command per process until it yields
2  the events it posted, which the host acts on and clears
3  ControlCode and ActionCode over every active character
4  the set's own event lines, which cut the camera and open the doors
```

Step 2 is the whole of how a script reaches the outside. The machine writes
`EVENT_TYPE + 1` into `scriptevent` and will not run another command until it
is cleared, so `camera`, `cinepak`, `redbook` and `sample` all wait on the same
handshake — which means a port that cannot play a film yet does not have to
pretend it can: it takes the request, clears the event, and the script carries
on. The `eventmask` a script writes now gates step 4 too, which is exactly what
it is for.

**And a puzzle runs.**

```
build/hlview --scene SE_CAM05 --char 0 --drive

frame    1: 3 script processes running
frame    2: 6 script processes running
frame    3: a script moved the floor: triangle 59 to 0 (4 written)
```

Three processes on the first frame are MAINSCRIPT's; the set's own start a
frame later, which is what `allnew` arranges by leaving behind a set number
that cannot match. Then the sewers' sluice runs: `SE`'s script tests which view
you are under and writes four collision triangle heights, one pair up to 885
and one pair down to 0, so the walkway you can stand on changes with the
camera. That is a puzzle from the disc, on the disc's own data, moving the
floor the character is standing on.

`main.c` keeps the viewer — `--play`, `--walk`, `--events`, the models and the
turntable — and what it does for `--drive` now is load a backdrop and draw.

→ [11-script-vm.md](../11-script-vm.md) 11.10

---

## Still open

* **The logic tables**, unchanged from session 9 and untouched this session:
  nothing on the retail disc fills `misc[0]`, so the table is resident and
  written by code. `CSHCODE.GAS` is still the file whose name suggests it.
  Until then both joypad-to-animation tables are ours — Quentin's, and now the
  follower's.
* **The 15th AI command**, which `LOGICS.INC` does not number.
* **The low byte of `cshBehaviour`** on the item and weapon sheets.
* **The films, and everything else a script asks for.** `cinepak`, `redbook`
  and `sample` post their event and the loop clears it without playing
  anything. That is phase 6 and phase 7.
* Unchanged: track 9 and `HIRESDATA`; `ZMODELT`'s sense; the non-uniform `face`
  elevation on the items; the three unreferenced films; the 125 unnamed world
  records; the seventeen unnamed GPU modules; the `SLP` payload; `gvar[0..2]`.

## TODO for session 11

### 1. The films — the main one

Every script that matters ends in one. `game.want_film` already carries the CD
block a script asked for and the loop already throws it away, so the hole is
exactly film-shaped and everything around it is in place:

* **The container is fully read** (9.2). `FILM` / `FDSC` / `CTAB`, chunk offsets
  relative to the film start, a 64-byte sync pad per chunk, then `STAB` with
  sixteen bytes per sample. The player seeks to a block and scans forward for
  the long `'1111'`, which is `CINEPAK.S`'s own `sync_header`.
* **The frame timing is known**: 12 fps throughout, frames 50 ticks apart at
  `CTAB`'s rate of 600.
* **The audio is read and verified** (9.3): signed 8-bit mono at 22,252 Hz, in
  16,696-byte blocks, reassembled in `STAB` order and checked by the step size
  across the joins. It is phase 7's to *play*, but nothing about it needs
  finding.
* **32 of the 36 films are placed** by their block offsets, against every
  `cinepak` operand on the disc (11.7).
* `tools/cinepak/filmls.py` and `filmwav.py` are the reference readers, and
  `assets/films.tsv` is the inventory.

So what is actually missing is **one decoder**. The video is `cvid` — plain
Cinepak, 320x240, and §5.2 already says not to transcribe the hand-written one:
the container is ours to read and the codec is a standard, published one.
Codebooks, 4x4 blocks, V1 and V4 vectors, inter and intra frames.

The shape it wants:

```
src/media/film.c     the container: seek to a block, find '1111', walk the
                     chunks, hand out video frames and audio blocks
src/media/cinepak.c  the cvid decoder, into an RGB16 surface
```

and then the loop's own handshake does the rest, because it is already the
original's: a script posts `EVENT_TYPE_CINEPAK + 1` and does not run another
command until it is cleared, so "play the film, then clear the event" is the
whole of the integration.

**Success criteria, in the shape this project uses.** Not "it looks right":

* `hlview --film 19` plays it in the window at 12 fps.
* `hlview --check-film` decodes **every frame of all 36 films** and reports
  zero decoder errors, zero chunks whose `STAB` does not account for their
  bytes, and the frame count per film against what `filmls.py` counts — two
  readers, one of them Python and written months earlier, agreeing.
* One frame of one film decoded in C and in Python, compared pixel for pixel.

### 2. Combat, or at least its movement half

`AIAttackCode` is ported down to the point where it needs `actStatus`, the hit
frames and the opponent's own joypad — which is `PCOL.TXT`, and phase 5. The
three sheets with `aiAttackPlayer` and the one with `aiShootPlayer` are waiting
for it, and `ACT_CREATED` already means a hunter can be put in a set.

### 3. The logic tables

Still a code search rather than a disc search: find what writes `misc[0]` of a
character sheet. `CSHCODE.GAS` is the file in the dump whose name suggests it.
Until then both joypad-to-animation tables are ours.

### Smaller, and still worth an hour each

* **The frame comparator** — render a scene, run the same scene under an
  emulator, diff. Three sessions have now wanted it, and session 9 spent a day
  on a number one frame diff would have settled.
* **The 15th AI command**, which `LOGICS.INC` does not number and one sheet
  uses.
* **The low byte of `cshBehaviour`** — 10, 20, 30, 40 or 250, on the item and
  weapon sheets only.
