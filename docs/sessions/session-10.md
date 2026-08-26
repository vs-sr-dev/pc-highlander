# Session 10 — Ramirez, and the word that says he follows

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

---

## Still open

* **The logic tables**, unchanged from session 9 and untouched this session:
  nothing on the retail disc fills `misc[0]`, so the table is resident and
  written by code. `CSHCODE.GAS` is still the file whose name suggests it.
  Until then both joypad-to-animation tables are ours — Quentin's, and now the
  follower's.
* **The 15th AI command**, which `LOGICS.INC` does not number.
* **The low byte of `cshBehaviour`** on the item and weapon sheets.
* **The scripts, running** — session 9's second item, not started.
* Unchanged: track 9 and `HIRESDATA`; `ZMODELT`'s sense; the non-uniform `face`
  elevation on the items; the three unreferenced films; the 125 unnamed world
  records; the seventeen unnamed GPU modules; the `SLP` payload; `gvar[0..2]`.

## TODO for session 11

1. **The scripts, running.** The VM is read (11) and every set carries one at
   `ScriptOffset`. The doorways now change sets and a second character now
   stands in them; a script is what is supposed to be watching. It is also what
   assigns Ramirez his sheet, which is why world record 1 carries none.
2. **Combat, or at least its movement half.** `AIAttackCode` is ported down to
   the point where it needs `actStatus`, the hit frames and the opponent's own
   joypad — which is `PCOL.TXT`, and phase 5.
3. **The logic tables**, still a code search: find what writes `misc[0]`.

And still worth an hour: **the frame comparator** — render a scene, run the same
scene under an emulator, diff. Two sessions have now wanted it.
