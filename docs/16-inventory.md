# 16 — The inventory, and the world that fills itself

Phase 5 owed two things: **item pickup and inventory**. Session 12 found the
door to them — `COMBAT.GAS`'s `COLLECTABLE` path writes a word called
`instpick` — and left it at that. This session opened the door and found rather
more than an inventory behind it.

The files are **`COLLECT.GAS`**, 1,795 lines by Andrew M. Harris dated 16 March
1995, and **`SETLOGIC.GAS`** and **`SCNLOGIC.GAS`**, which had never been read.
The last two are not about items at all. They are how a set gets its
population, and until this session the port had none: every character in
`--drive` was put there by hand.

## 16.1 There is no inventory

A world record's `wstParent` is who owns it. Carrying something **is** that
field pointing at you, and that is the whole data structure. There is no list,
no capacity, no per-character array — just one long on each of the 197 records.

Everything follows from it. Giving an object away is one store (`giveobj`).
Asking what somebody has is a walk over the world table. And the screen's left
and right, which look like a list, are a **search**:

```
nextobj:  step one record forward from the one on show - or from record 0 if
          that is the dummy - and take the first whose wstParent is the player
lastobj:  the same backwards
```

Running off either end is not a wrap. The original loads the *offset*
`wstParent` as though it were an address and immediately subtracts it back off,
which leaves zero — the **dummy object**, which is the bare hand. So the list
you page through is: nothing, then what you have, then nothing.

**49 of the 99 collectables on the disc start in somebody's keeping**, and not
one of those somebodies is Quentin. They belong to the people you meet. Which
makes the next section the game's actual economy.

## 16.2 `DeadControl` drops what he was carrying

`AICTRL.GAS`'s `DeadControl` runs on the one frame a dying character's radius
goes to zero — the original's own test for "dead, instead of dying" is that the
radius is already zero — and then it walks all 512 world records. For every one
whose parent is him: clear the parent, set `WSTRegistered`, copy **his** set and
his three coordinates onto it, and build it an entry with `aiNop`.

That is the same move as dropping something, aimed at somebody else's feet.
It is how most of the game's items reach the floor.

## 16.3 The way in is `instpick`

`COMBAT.GAS`'s pair loop carries it, added to the file on 09/03/95 — "also
added COLLECTABLE code, to allow pickups". A pair where **exactly one** of the
two carries `WSTCollectable` does no combat at all, and if the other one is the
player and their circles overlap, the item's table entry goes into `instpick`.

The "exactly one" is an `xor` of the two flag bytes and one `btst`, which is
also why two items lying inside each other are not a pickup: they are two
bodies, and they push each other apart like anything else.

Two details make it behave, and both are one line each:

* only one item may claim `instpick` in a frame, so walking into a heap offers
  you one of them rather than all of them;
* the item's own `PICKUP` flag is set while you stand inside it and cleared the
  frame you step out — so it offers itself once, and an item you *refused* does
  not nag you until you have walked away and come back.

And `createchar` sets `PICKUP` on the entry it builds for a **dropped** object,
which is the same rule pointed the other way: you are standing on the thing you
have just put down, and without that line it would be offered straight back.
`DeadControl` zeroes the flags word instead, so what a dead man drops *is*
offered at once. That is the difference you would want between the two.

## 16.4 `COLLECT.GAS` is the last module in the frame

`MAIN.S`'s `update` runs `ANIMCODE`, `PPCOLL`, `ENGINE`, `BITMAP`, `TEXTS`, and
then `COLLECT`. That position is the design: when COLLECT opens a screen it
spins its own loop inside that one call, drawing its own picture and reading the
pad itself, and the game behind it does not advance until a button ends it. The
inventory is modal, and not by interpretation.

Its own comment block is the specification, and the five buttons are its five
lines:

```
left/right - change 'current' by searching thru ws for next/last "owned"
Reject pickup OR Drop 'current' and end - A
Accept any pickup object and end - B
Choose 'current' as inhand, accept any pickup and end - C
```

with OPTION doing what C does but *without using* what it chose — one skipped
`CALL_SUB` is the entire difference between the two buttons — and `*` or `#`
leaving. When the screen was opened by walking onto something, the four
directions are dead: a pickup is take it or leave it.

`darken_screen` halves the backdrop by shifting the whole 32-bit word (two
pixels) right one and masking the carry out of each field, `%0111101111011111`
twice over. Then one model turns in front of it, at −600 units.

## 16.5 Two world-state bits, and they are the first two in the game

`WSB.INC` includes `ROBSWSB.INC` before anything else, and it declares exactly
two:

```
nextwsb COLLECT_PREVENT   ; when set you can't pick anything up using option button
nextwsb SCRIPT_PICKUP     ; new one to let me pick things up automatically
```

So they are world-state bits **0 and 1**. `SCRIPT_PICKUP` is half of a pair:
the script command `pickup` — `force_pickup` in `SCRIPT.GAS`, opcode 71 —
**posts no event and waits for nothing**. All it does is write the world record
it names into `currws` and that record's model into `currmod`, and the bit is
what tells COLLECT to take it without a screen.

This port had opcode 71 posting an `EVENT_TYPE_RESTOREITEM`, which belongs to
`restore` and not to it. Fixed, and the fix is visible in the checks: three of
the groups turn out to raise `COLLECT_PREVENT` and drive the pickup from the
script instead, with `currws` already holding the object they mean.

`testowner` had the matching problem from the other end. `wstParent` is an
address in the pre-relocation world table at `$15458`, and the machine was
comparing that address against a record *index*. Resolving it matters more than
it looks: zero has to stay distinct from record 0, because record 0 is the
player and "unowned" is not "his".

## 16.6 `wstUsage`, and what bit 7 is

Using an item is `douse`, and what using means is one byte — `wstUsage`, which
is `wstSanity`, because a loaf of bread has no sanity. COLLECT.GAS lists the
bits in a comment:

```
0  identity change item (dword = offset from cs of charsht)
1  anim script use item (dword = anim num, +hp)
2  item put in hand now
3  put item in temp - restore 'weapon' item after anim
4  change to barehand weapon (+animsheet) now
5  special script use item      * not implemented *
6  x4 size
7
```

Bit 7 the comment leaves blank and the code answers: bit 6 halves the distance
the screen holds the model at, and bit 7 quarters it again. They are how close
to bring a small thing. **88 of the 99 collectables set bit 7** and 23 set bit
6, which is the right shape for "most of what you carry is small".

Bit 1 is the one that does not act. It puts a process on the script machine —
`UseItem`, MAINSCRIPT's fourth block, the one nothing in any script spawns and
that §11.9 identified as "plays animation r0, adds r1 to Life and restores the
item if r2". The engine starts it with the two `wstDword` bytes in r0 and r1.
`wstDword` is `wstPerson` and the byte after it, which on a person are the
personality and the strength; an item is never in a fight — PPCOLL drops the
pair before it reads either — so the two bytes are free.

Sixty-three of the collectables carry it, and between them they hold **three**
values:

| dword | animation | life |  how many |
|---|---|---|---|
| `$1C20` | 28 | +32 | 23 |
| `$1D30` | 29 | +48 | 22 |
| `$1D40` | 29 | +64 | 18 |

Three consumables, two eating animations. Nothing on the disc uses bit 0, the
identity change.

## 16.7 A weapon is a sheet, and `animsheet` is where it goes

`chooseit` sends a record with `WSTWeapon` down `wpn`, which points **`animsheet`**
at that weapon's own character sheet and loads its animations through
`CSHCODE.GAS` — which turns out not to be the logic-table file its name
suggested, but the loader and freer for exactly this.

That is `AICTRL.GAS`'s `.weapon_action` from the other end. Session 12 worked
out what it did by measuring the animations; this is the code that sets it up,
and the two agree. In this port the player carries every bank in one bundle, so
`animsheet` comes to an offset — and the offset is now derived the way the game
derives it, from the sheets that a `WSTWeapon` record wears, rather than from
"one model and some animations", which also caught sheet 17 and was one out.

The four banks, each with **exactly nine animations that swing** — seven attacks
and two guards, the same shape in every one:

```
with nothing in his hands:  animations 0..29,    for 2 (x5) 4 (x1) 30 (x1) 5 (x2), reaching 250
sheet 22 (world 114):       animations 30..57,   for 30 (x9),                      reaching 375
sheet 23 (world 27):        animations 58..85,   for 127 (x7) 30 (x2),             reaching 625
sheet 24 (world 131):       animations 86..113,  for 80 (x7) 5 (x2),               reaching 1500
```

Bank 3 reaching 1,500 units is the thing you shoot with, and `ANIM.GAS` says so
in a way nothing else does: the in-hand object's **`wstLife` is its ammunition
count**, decremented one per triggered frame and never written back below zero.
Five records carry `WSTAmmo`.

One refinement to §15.3 while we are here: his bare hands are not uniformly 2.
Five of his seven attacks are, one is 4, and **animation 21 hits for 30** — as
hard as the first sword.

## 16.8 And underneath all of it, the set fills itself

None of the above has anywhere to happen unless the world puts objects in
sets, and that is `SETLOGIC.GAS` and `SCNLOGIC.GAS`.

**`GetSet`** is two loops and no cleverness. `ParseACT` throws out every active
character whose world record names a different set. `ParseWST` then walks the
world table and takes in everybody it names — every record with a character
sheet whose `wstSet` matches — registered, and given an entry with
`cshBehaviour` as its AI command. A set's population is a query over the world
table; nothing is stored per set, which is why the 48 set files carry no cast
list and the 197 records carry a set each.

`wstSet` is the scene id masked to `$FFC0`, so it is the group times 64, and
`CurrSet` is the same mask over the view on screen. `MAIN.S` says it in as many
words: `move.w #SCENE_DUN1_1&$ffc0,CurrSet`.

`ParseWST`'s own comment is careful — "this bit **registers but does not
create** characters in current set" — and the creating is a separate module,
**`CHARNEWSCN`** in `SCNLOGIC.GAS`, run on every scene change. Its master loop
is four tests:

* not `WSTDeactivated` — the bit the script command `activation` turns on and off;
* **not owned**, `cmpq #0,reg19 / jr NE,.outside`, and reg19 is `wstParent`.
  This is the guard that keeps what somebody is carrying from standing on the
  floor beside him, and it is why `acceptobj` never needs to clear `wstSet`;
* within `$4000` of the camera in x, and within `$4000` in z.

The distance is measured from the **camera**, not from the player: reg12 and
reg13 are loaded from `CurrScene`+20 and +28. Which is a second, independent
confirmation of the scene footer's shape — id, nine matrix words, then three
position longs at exactly 20, 24 and 28 (§7.5).

Walk into `DUN1` now and it has **two Hunters and three things on the floor**,
none of them put there by a command-line flag:

```
build/hlview --scene DUN1_CAM04 --char 0 --drive

the set populates itself from the world table: 5 records registered
  player    world   0 sheet  1  at (  1028,  -158) tri   2  nop
  here      world   1 sheet  7  at (  1028,  -158) tri   2  follow player
  here      world 104 sheet  2  at (  5930, 13166) tri  47  attack player
  here      world 105 sheet 25  at ( -5891,  5100) tri 187  nop
  here      world 106 sheet 26  at ( -6168,  8105) tri 180  nop
  here      world 107 sheet 27  at (  9054,  5891) tri 219  nop
  here      world 109 sheet  2  at ( -2056, 15934) tri 156  attack player
```

Ramirez is there because the harness put him there. The world table has him
**`WSTDeactivated`** at boot, and a script is what wakes him.

Five sheets — 17 and the four tumblers 18 to 21 — name a track-5 slot but have
no *bundle* in it, because `bundle_at` wants a model that hangs on no origin and
publishes at least one, and a thing with no joints publishes none. Their slot
holds a single model, exactly like an item's.

## 16.9 What is checked

```
build/hlview --check-inventory

the world table: 197 records in use, 99 collectable, 7 of those weapons
  49 start in somebody's keeping and 0 are registered - nothing is in the world
  until ParseWST puts it there
ParseWST over the 48 scene groups the sets name: 144 bodies, 50 of them
  collectable, 0 records claimed twice
  147 records are free, awake and wearing a sheet; 3 of them never got a body
    0(g0,s1) 143(g30,s-1) 144(g31,s-1)
pickup and drop: 55 collectables tried where their own group put them, 0 on a
  piece of floor he cannot reach
  55 offered themselves on exactly one frame and 55 went into the pocket
  51 were found again by the screen's own search and 51 came back to the floor
  at his feet
  4 were left alone because their set had taken the pickup over
  0 offered themselves twice over without being left and returned
DeadControl: 34 characters died with something in their pockets, carrying 54
  objects between them
  54 fell out, 54 of them standing where he fell
```

The three records with no body are the player, who has no set of his own, and
143 and 144, whose `wstSheet` resolves to no sheet at all — an oddity in groups
30 and 31 that predates this and is now written down.

The last line of the pickup block is the one that would go quietly wrong. Zero
items offering themselves twice is `createchar`'s single `bset`, and the check
only sees it because the harness walks the player to the far end of the mesh
between one trial and the next — which it has to, because an item he has
refused stays refused until he moves.

## 16.10 Still open here

* **Two characters squared up face to face never land a blow.** `AIAttackCode`
  cycles attack, defend and pause, and on the pause frames it presses nothing —
  which is faithful, `.pause` in `AICTRL.GAS` really does fall through with only
  the rotate bits set. But dropping the buttons makes `ActionCode` pick the
  stand row, which restarts the swing before it reaches the frame the blow is
  drawn on. `--check-combat`'s duels land blows because it places the pair at
  assorted angles and the `diff < 10` test forces a straight attack whenever one
  of them is not squared up; `--drive --fight`, which places them exactly
  opposite, never does. This is session 12's code and predates this session —
  it reproduces unchanged at the previous commit — and it is a good candidate
  for the frame comparator.
* **The in-hand model is the 16th piece.** `EVENT.GAS`'s `changetomod` says so:
  "immediately change model 16 of player character to object specified", found
  by fifteen loads down the draw chain. The port has `inhand` and knows which
  model it is; it does not yet hang it on him.
* **Bit 5 of `wstUsage`**, "special script use item", is marked *not
  implemented* in 1995 and nothing sets it.
* **The low byte of `cshBehaviour`** — 10, 20, 30, 40 or 250 — is still
  unexplained, and is not `wstUsage`: it is on the sheet, not the record.
