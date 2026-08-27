# Session 13 — The pockets, and the world that fills itself

Session 12's TODO put the frame comparator first and inventory second. The
comparator is still first and still not built — it needs an emulator, and
BigPEmu is in another pipeline — so this session took the second item, which
was described as "the rest of phase 5".

It was not the rest of phase 5. `COLLECT.GAS` is 1,795 lines by Andrew M.
Harris and it is the whole inventory *screen*, not a pickup; and behind it sit
**`SETLOGIC.GAS`** and **`SCNLOGIC.GAS`**, two files nobody had opened, which
turn out to be how a set gets its population at all. Until this session every
character in `--drive` was put there by a command-line flag.

→ [16-inventory.md](../16-inventory.md)

---

## 1. There is no inventory

`wstParent` is who owns you. Carrying something **is** that field pointing at
you, and that is the entire data structure — one long on each of the 197 world
records, no list, no capacity, no per-character array.

Which makes the screen's left and right a *search* rather than a list, and
makes both ends of the search the bare hand: the original loads the offset
`wstParent` as an address and immediately subtracts it back off, leaving zero,
which is the dummy object. You page through nothing, then what you have, then
nothing.

And it makes one number the game's economy. **49 of the 99 collectables start
in somebody's keeping**, and not one of those somebodies is Quentin. They
belong to the people you meet, and `DeadControl` is how they change hands.

## 2. The set fills itself, and that was the real gap

`SETLOGIC.GAS`'s `GetSet` is two loops. `ParseACT` throws out everybody whose
world record names a different set; `ParseWST` takes in everybody it names —
every record with a sheet whose `wstSet` matches, registered and handed its
`cshBehaviour` as an AI command. A set's population is a **query over the world
table**. Nothing is stored per set, which is why the 48 set files carry no cast
list.

But `ParseWST`'s own comment says it "registers but does not create", and the
creating is a third module, `CHARNEWSCN` in `SCNLOGIC.GAS`, run on every scene
change. Four tests decide who has a body: not `WSTDeactivated`, **not owned**,
and within `$4000` of the camera in x and in z.

The "not owned" is the guard that had been bothering me for an hour — without
it, everything in everybody's pockets would stand on the floor beside them, and
`acceptobj` never clears `wstSet`. And the distance is measured from the
**camera**, `CurrScene`+20 and +28, which is a second and completely
independent confirmation of the scene footer's shape: id, nine matrix words,
then three position longs at exactly 20, 24 and 28.

Walk into `DUN1` now and it has two Hunters and three things on the floor,
without `--fight`:

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

Ramirez is there only because the harness put him there. The disc has him
**`WSTDeactivated`** at boot, which is a thing worth knowing about the game.

## 3. Three one-line rules, and the check that sees each of them

The pickup itself is four lines of `COMBAT.GAS` riding on the pair loop, and
three separate single lines make it feel right rather than mechanical:

* **only one item may claim `instpick` per frame**, so a heap offers you one
  thing, not all of them;
* the item's `PICKUP` flag stays set while you stand in it, so something you
  *refused* does not nag you until you have walked away;
* and `createchar` sets `PICKUP` on the entry it builds for a **dropped**
  object — you are standing on what you just put down. `DeadControl` zeroes
  the flags word instead, so what a dead man drops *is* offered at once, which
  is the difference you would want between the two.

That third one I missed on the first pass, and the check found it: 38 of 38
items offering themselves twice over. It is one `bset`.

The second one turned the harness inside out before I believed it. Two items in
a row would fail, and the reason was that the harness was testing a player who
never took a step. `walk_away` — put him at the far end of the mesh between
trials — is not tidying up, it is the only way to test the rule at all.

## 4. Two commands this port had wrong

**`pickup`, opcode 71**, is `SCRIPT.GAS`'s `force_pickup`, and it posts no
event and waits for nothing: it writes the record it names into `currws`, and
world-state bit 1 — `WSB_SCRIPT_PICKUP`, "new one to let me pick things up
automatically" — is what tells COLLECT to take it with no screen. This port had
it posting an `EVENT_TYPE_RESTOREITEM`, which belongs to `restore`.

The fix is visible from the outside: four of the 55 collectables now decline to
be picked up by hand, because their group has raised `WSB_COLLECT_PREVENT` —
bit 0, "when set you can't pick anything up using option button" — and has
`currws` already holding the object it means. The pair is being used exactly as
ROBSWSB.INC describes it.

**`testowner`** had the matching bug from the other end: `wstParent` is an
address in the world table at `$15458` and the machine was comparing it against
a record *index*. Zero has to stay distinct from record 0, because record 0 is
the player and "unowned" is not "his".

## 5. `wstUsage`, and the blank line in the comment

Using an item is one byte, `wstSanity` on anything you can pick up, and
COLLECT.GAS lists its bits — except bit 7, which the comment leaves blank. The
code answers: bit 6 halves the distance the screen holds the model at and bit 7
quarters it again. They are how close to bring a small thing, and **88 of the
99 collectables set bit 7**.

Bit 1 does not act at all. It starts `UseItem` — MAINSCRIPT's fourth block, the
one nothing in any script spawns, which §11.9 had already identified from the
inside as "plays animation r0, adds r1 to Life and restores the item if r2".
The two bytes come from `wstDword`, which is `wstPerson` and the byte after it:
free on an item, because PPCOLL drops a collectable pair before it reads
either. Sixty-three items carry it and they hold **three** values between them
— animation 28 for +32 life, and animation 29 for +48 and +64.

## 6. `--weapon` is a thing you pick up now

`chooseit` points **`animsheet`** at a weapon's own character sheet, and
`CSHCODE.GAS` — 128 lines whose name suggested logic tables — turns out to be
the loader and freer for exactly that, which closes one thread of session 12's
TODO 3 without opening anything.

`--weapon N` now finds the world record wearing that sheet, puts it in his
pocket with `collect_accept` and in his hand with `collect_choose`, and the
bank falls out of the sheet chain rather than out of a flag. Deriving it the
game's way — the sheets a `WSTWeapon` record wears — also corrected it: the old
search for "one model and some animations" caught sheet 17 as well and came out
one high on banks 2 and 3.

Four banks, each with **exactly nine animations that swing**, seven attacks and
two guards:

```
with nothing in his hands:  animations 0..29,    for 2 (x5) 4 (x1) 30 (x1) 5 (x2), reaching 250
sheet 22 (world 114):       animations 30..57,   for 30 (x9),                      reaching 375
sheet 23 (world 27):        animations 58..85,   for 127 (x7) 30 (x2),             reaching 625
sheet 24 (world 131):       animations 86..113,  for 80 (x7) 5 (x2),               reaching 1500
```

One refinement to §15.3: his bare hands are not uniformly 2. Five of the seven
are, one is 4, and **animation 21 hits for 30** — as hard as the first sword.

And `ANIM.GAS` names the field nothing else could: the in-hand object's
`wstLife` is its **ammunition count**, taken down one per triggered frame.

## 7. The screen draws

`MAIN.S` runs `COLLECT` last of the six modules in a frame, after the 3D
engine, the bitmaps and the text, and that position is the design: it spins its
own loop inside one call and the game does not advance until a button ends it.
So it is modal in the port too, and `game_frame` hands it the whole frame.

`darken_screen` halves every channel by shifting the whole 32-bit word right
one and masking the carry out of each field. Then one model turns in front of
it at −600 units, pulled closer by the two `wstUsage` bits.

```
build/hlview --scene DUN1_CAM04 --char 0 --drive --weapon 2 --pad -:6,option:1,-:8
```

opens it on the sword.

## 8. What is checked

```
build/hlview --check-inventory

the world table: 197 records in use, 99 collectable, 7 of those weapons
  49 start in somebody's keeping and 0 are registered
ParseWST over the 48 scene groups the sets name: 144 bodies, 50 of them
  collectable, 0 records claimed twice
  147 records are free, awake and wearing a sheet; 3 of them never got a body
pickup and drop: 55 collectables tried where their own group put them
  55 offered themselves on exactly one frame and 55 went into the pocket
  51 were found again by the screen's own search and 51 came back to the floor
  4 were left alone because their set had taken the pickup over
  0 offered themselves twice over without being left and returned
DeadControl: 34 characters died with something in their pockets, carrying 54
  objects between them
  54 fell out, 54 of them standing where he fell
```

Eight checks now, and the other seven are unchanged — except `--check-script`,
which executes 586,632 commands instead of 585,226 because `testowner` now
resolves and the scripts take different branches.

The three records with no body are the player, who has no set of his own, and
143 and 144, whose `wstSheet` resolves to no sheet at all.

---

## Something that turned out not to be mine

`--drive --fight` does not land a blow, and neither does it at the previous
commit — I built HEAD in a worktree to be sure. The cause is precise:
`AIAttackCode` cycles attack, defend and pause, and its `.pause` really does
press nothing but the rotate bits. Dropping the buttons makes `ActionCode` pick
the stand row and restart the swing before it reaches the frame the blow is
drawn on. `--check-combat` gets its eleven kills because it places the pair at
assorted angles, and `AIAttackCode`'s `diff < 10` test forces a straight attack
whenever one of them is not squared up; `--drive --fight` places them exactly
opposite, and they circle each other for ever.

It is session 12's code, it is written down now (§16.10), and it is a good
candidate for the frame comparator rather than for another hour of guessing.

## Still open

* **The comparator**, unchanged and now first for the fourth session running.
* **The in-hand model is the 16th piece of the player's chain** — `EVENT.GAS`'s
  `changetomod` says so outright, "change model 16 of player character", found
  by fifteen loads down the draw list. The port knows what is in his hand and
  does not yet hang it on him.
* **A projectile**, unchanged: bank 3's 1,500-unit reach is applied as an
  ordinary blow.
* **The logic tables.** `CSHCODE.GAS` is ruled out — it is `animsheet`'s loader
  — so what writes `misc[0]` is still unfound.
* **The 15th AI command**; **the low byte of `cshBehaviour`**, which is *not*
  `wstUsage` since it lives on the sheet rather than the record; **the combat
  sounds**, phase 7.
* Unchanged: track 9 and `HIRESDATA`; `ZMODELT`'s sense; the 125 unnamed world
  records; the seventeen unnamed GPU modules; the `SLP` payload; `gvar[0..2]`.

## TODO for session 14

### 1. The frame comparator, with the emulator that now exists

BigPEmu is available. Render a scene in `hlview`, run the same scene under it,
diff. It settles the Cinepak colour rounding (§9.5), whether a knockback code
picks the reaction this port guesses (§15.2), the combat *timing*, and now the
face-to-face stand-off above (§16.10) — five reasons in four sessions.

### 2. The face-to-face stand-off

Whether or not the comparator arrives first, this is a real fight bug and it is
narrow: the AI's pause frames restart the swing. Either `ActionCode` should not
re-pick while an animation is playing, or `AIAttackCode`'s melee branch should
fall through to `.default` the way its move branch does. `AICTRL.GAS` will
settle it; read `ActionCode` properly rather than the AI.

### 3. The in-hand model

Sixteen pieces, not fifteen. `changetomod` in `EVENT.GAS` walks fifteen entries
down the draw chain and rewrites the sixteenth, and `ANIM.GAS` poses it. It is
the last thing between the inventory and looking like the game.

### 4. The film audio, phase 7's

Carried over unchanged for the third session: `film.c` hands out every audio
block in order, signed 8-bit mono at 22,252 Hz, and the films run on their own
timestamps, so the video is the clock. The combat sounds want the same mixer,
and so do the four `sound*` entries on every character sheet.

### Smaller, and still worth an hour each

* **The films, named by looking at them** — 15, 28 and 33 have no trigger
  anywhere on the disc.
* **World records 143 and 144**, whose `wstSheet` points at nothing, in groups
  30 and 31.
* **The 15th AI command**, which `LOGICS.INC` does not number.
