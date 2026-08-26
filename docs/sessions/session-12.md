# Session 12 — The fight

Session 11's TODO put combat first: "`AIAttackCode` is ported down to the point
where it needs `actStatus`, the hit frames and the opponent's own joypad — which
is `PCOL.TXT`, and phase 5."

`PCOL.TXT` turned out to be a design note rather than the thing itself. The
thing itself is **`COMBAT.GAS`**, 1,144 lines of GPU by Robert C. Dibley,
"loosely based on work by Andrew M. Harris" — a file with a dated history at
the top that reads like a list of everything that went wrong with the fights in
1995. It is in, and so is the other half of `AIAttackCode`, and phase 5's
success criterion is met: you can fight a Hunter and one of you dies.

→ [15-combat.md](../15-combat.md)

---

## 1. The moves are the animations

There is no table of moves in this game. Every animation frame carries
`animHit`, `animRange`, `animDirAz` and `animSprAz` — a hit value, a reach, a
direction and an arc — and `COMBAT.GAS` reads them straight out of whichever
frame the character is on. Session 2 read those fields out of the format;
what they are *for* is this.

So "which animations are the sword swings" is a question the disc answers.
`hlview --list-attacks` asks it, and the answer is not scattered: **87 of the
285 animations on track 5 carry a hit value**, and in every bundle that has a
full bank they sit at **19 to 25** for the attacks and **26 and 27** for the
guards.

## 2. Which lets one table drive everybody, and now says so

The joypad table this port drives every character with was read off Quentin's
thirty animations in session 8 — 6 the stand, 10 the walk, 15 and 16 the turns.
Everybody else is then driven through the same numbers, which is only sound if
the numbering belongs to the format rather than to Quentin.

Combat needs seven more roles, and the data names all seven, because a frame
records the highest point of the pose it describes. An animation that starts at
414 and ends at 82 has put the character on the floor:

```
 0   612 back, 232 down   ends 111 high    falls backwards
 1   189 left, turns 144  ends 104 high    spins and falls
 2   343 back, 74 right   ends  83 high    falls back-right
 3   332 back, 136 left   ends  82 high    falls back-left
 4   416 back             ends 424 high    knocked back, still standing
 5   325 forward          ends 406 high    knocked forward
 8   nothing moves        stays  80 high   lying there
```

Four falls and two staggers — one of each per knockback quadrant, and
`STRUCDEF.INC` names two of the four codes outright, `00 back` and `10
forward`. `--check-combat` asserts all fourteen roles in **every bundle with a
full bank, six of them, and finds no departures.** Nothing in that test knows
which character it is looking at.

## 3. Quentin's 114 animations are 30 plus three weapons

His sheet declares thirty. There are exactly three sheets on the disc with one
model and 28 animations — 22, 23 and 24, the weapons — and `30 + 28 + 28 + 28 =
114`. The arithmetic is confirmed by where the hit values fall: 19..27, then
again at **49..57, 77..85 and 105..113**, which is the same 19..27 at offsets
0, 30, 58 and 86.

That is `AICTRL.GAS`'s `.weapon_action`, which looks an animation number up in
the sheet of whatever the player is holding before falling back to his own.
**Picking up a sword does not change the table; it changes which bank the
table's numbers land in.** And the four banks say what they are: his hands hit
for 2, bank 1 for 30, bank 2 for 127, and bank 3 has a reach of a thousand
units and an arc of five degrees, which is a thing you shoot with.

`--weapon N` puts one in his hands, and it is the difference between winning
and losing.

## 4. `PPCOLL`, and the two rules that keep it a duel

`src/game/combat.c` is the pass, run where `MAIN.S` runs it — after the
animation player, before the 3D engine. A positive hit adds to the *other*
man's damage and a negative one adds to your own, which is how a guard
subtracts from what is coming at you; what is left comes off `wstLife` and sets
`FSAHit` with two bits saying which way you were struck; and a blow that
connected but was entirely swallowed is a **parry**, which gets `FSAShield`
instead. Then, separately, two overlapping circles put each other back where
they started the frame.

Two rules keep it from being a free-for-all, and both are dated in the file's
own history: **exactly one of the pair must be the player** (01/05/95,
"modified to prevent Hunters killing each other"), and neither may already be
shielding.

Note what the first rule does *not* say: the player and his companion are one
of each, so they are a duel. Walk into `DUN1` swinging with Ramirez behind you
and you kill him in eight frames. That is the rule as written.

There is also a detail worth the space. The arc a blow covers is widened by
`40 * their radius / distance` — the `shooting_fix` of 18/05/95 — so **your
spread grows with the other man's size, not your own**. A big target close up
is harder to miss, which is the correct way round and not the obvious one.

## 5. `HitControl`: the joypad nobody pressed

`ControlCode`'s five tests are in an order that is the whole design — being
hit, then locked, then dead, then script-driven, then the pad — and
`HitControl` does not read a joypad. It **writes** one: `JOY_HIT`, plus
`JOY_KILL` if the life points have gone, plus the knockback bits. The logic
table then turns that into a reaction exactly as it turns any other joypad into
a walk. Being knocked over is a button press nobody pressed, and that is why
there is a `JOY_HIT` at bit 30 of a joypad long in `LOGICS.INC` at all.

## 6. Two things the source itself settles

**`actStatus`'s clock is not the frame counter.** `AIAttackCode` advances a
three-state machine — attack, defend, pause — with `btst` on reg1, and reg1 is
what `ControlCode` loaded the frame count into. But `AIRandomCode`, called two
instructions earlier, **overwrites reg1** with that frame count squared, mixed
with both characters' coordinates and rolled a byte at a time. The rhythm of a
fight is a hash of where the two of them are standing. This port had it as the
frame counter for an hour, and the fights came out mechanical in exactly the
way you would expect.

**The Jaguar GPU has one delay slot**, and COMBAT.GAS proves it from its own
arithmetic:

```
	cmpq	#0,ptemp3		; check for sign of attack
	jr	pl,player2
	add	ptemp3,att2		; use as attack value
	add	ptemp3,att1		; setup defence value
	jr	player2
	sub	ptemp3,att2		; remove attack value if not needed
```

With one, a negative hit adds to `att2`, then to `att1`, then takes it back off
`att2` — leaving the defence on `att1` alone, which is what the comments say.
With two, the second `add` would run on both paths and the comments would be
wrong. That is worth having settled: every `.GAS` file in the dump is read
under it.

## 7. And the table cannot tell two buttons apart, on purpose

The AI reissues its joypad every frame while attacking, and the button comes out
of that hash — `FIRE_C` on half the frames and `FIRE_B` and `FIRE_C` on the
other half. A table that told those apart would restart the swing every other
frame and the blow would never reach the frame it was drawn on.

This one cannot: the search wants every bit of the mask held and `actAction`
records the pad **masked**, so both pads match the row whose mask is `FIRE_C`
alone and both leave the same value behind. That is why `AIAttackCode` needs no
lock around a swing — and it is a real constraint on the shape of the missing
`BO_LOGICS_1`, arrived at by trying the other thing first. Locking the swing
freezes a character out of `ControlCode` altogether, and three characters end
up standing in a field swinging at nothing.

## 8. What is checked

```
build/hlview --check-combat

animation roles: 6 bundles with a full bank, 0 departures from the convention
duels: 11 fought one on one, 11 ended with somebody dead, 617 frames each
  the hunter died 4 of the 5 times the player had a weapon and 1 of the 6 he
  had his hands
  0 life values that went up, which is what says no life byte wrapped
  two hunters within reach of each other for 1185 frames of 1,200, and 0
  points of damage between them
```

The last line is the one that would go quietly wrong: it is COMBAT.GAS's rule
of 1/05/95, and two hunters standing on top of each other for 1,185 frames
without exchanging a point is the only way to see that it is still there.

And in the game rather than in the harness:

```
build/hlview --scene DUN1_CAM04 --char 0 --drive --fight --weapon 1

hunter: world 3, sheet 2, attack player, bundle 1 with 28 animations
weapon 1: animations 30..57 of the bundle
frame   17: hunter is hit, life 40
frame   39: hunter is hit, life 10
frame   48: hunter is killed, life 0
```

Leave `--weapon` off and he loses, because a man hitting for 2 against one
hitting for 20 loses.

---

## Still open

* **Inventory**, which is the other half of the same two files: `PPCOLL`'s
  `COLLECTABLE` path is the pickup and `DeadControl` drops what a dead man was
  carrying into the world as fresh characters. Neither is ported.
* **A projectile.** The two `aiShoot` commands run the attack machine, but
  bank 3's thousand-unit reach is applied as an ordinary blow.
* **The logic tables**, unchanged since session 9: nothing on the retail disc
  fills `misc[0]`, so the real `BO_LOGICS_*` are resident and written by code.
  `CSHCODE.GAS` is still the file whose name suggests it. §15.9 now says
  something about what one of them must look like.
* **The 15th AI command**, which `LOGICS.INC` does not number.
* **The low byte of `cshBehaviour`** — 10, 20, 30, 40 or 250, on the item and
  weapon sheets only. Now that the weapon sheets are identified as sheets 22,
  23 and 24, whose low bytes are 250, 10 and 10, this is a smaller question
  than it was.
* **The combat sounds** — `soundKIA`, `soundHIT`, `soundATT`, `soundPAR`, four
  entries on every character sheet, played by COMBAT.GAS itself. Phase 7.
* Unchanged: track 9 and `HIRESDATA`; `ZMODELT`'s sense; the non-uniform `face`
  elevation on the items; the 125 unnamed world records; the seventeen unnamed
  GPU modules; the `SLP` payload; `gvar[0..2]`.

## TODO for session 13

### 1. The frame comparator

Five sessions have wanted it, and it now has three jobs rather than one: it
would settle the Cinepak colour rounding (§9.5), it would settle whether a
knockback code picks the reaction this port guesses it does (§15.2), and it is
the only way to check the combat *timing* against the machine that shipped.
Render a scene, run the same scene under an emulator, diff.

### 2. Inventory, which is the rest of phase 5

`PPCOLL` already walks past the pickup: a `WSTCollectable` world record inside
the player's radius sets `instpick` and the `PICKUP` flag, and `DeadControl`
turns what a dead character was carrying into fresh world records with
`WSTRegistered` set. Both are read and neither is ported. It is also what makes
`--weapon` a real thing rather than a switch: the weapon banks are already
identified, so what is missing is picking one up.

### 3. The logic tables

Still a code search rather than a disc search: find what writes `misc[0]` of a
character sheet. `CSHCODE.GAS` is the file in the dump whose name suggests it.
§15.9 is now a constraint the real table has to satisfy, which is a way to
check a candidate when one turns up.

### 4. The film audio, which is phase 7's

Carried over from session 11 and unchanged: `film.c` hands out every audio
block in order, signed 8-bit mono at 22,252 Hz, and the films run on their own
timestamps — so the video is the clock and the audio is what gets locked to it.
The combat sounds want the same mixer.

### Smaller, and still worth an hour each

* **The films, named by looking at them.** Carried over: films 15, 28 and 33
  have no trigger anywhere on the disc, and a frame of each would name them.
* **The 15th AI command**, which `LOGICS.INC` does not number and one sheet
  uses.
* **The low byte of `cshBehaviour`**, now that the three sheets that matter
  most are known to be the weapons.
